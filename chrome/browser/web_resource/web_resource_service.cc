// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/web_resource/web_resource_service.h"

#include "base/command_line.h"
#include "base/file_path.h"
#include "base/string_util.h"
#include "base/string_number_conversions.h"
#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_thread.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/sync/sync_ui_util.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/net/url_fetcher.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/notification_type.h"
#include "chrome/common/pref_names.h"
#include "googleurl/src/gurl.h"
#include "net/base/load_flags.h"
#include "net/url_request/url_request_status.h"

namespace {

// Delay on first fetch so we don't interfere with startup.
static const int kStartResourceFetchDelay = 5000;

// Delay between calls to update the cache (48 hours).
static const int kCacheUpdateDelay = 48 * 60 * 60 * 1000;

}  // namespace

const char* WebResourceService::kCurrentTipPrefName = "current_tip";
const char* WebResourceService::kTipCachePrefName = "tips";

class WebResourceService::WebResourceFetcher
    : public URLFetcher::Delegate {
 public:
  explicit WebResourceFetcher(WebResourceService* web_resource_service) :
      ALLOW_THIS_IN_INITIALIZER_LIST(fetcher_factory_(this)),
      web_resource_service_(web_resource_service) {
  }

  // Delay initial load of resource data into cache so as not to interfere
  // with startup time.
  void StartAfterDelay(int delay_ms) {
    MessageLoop::current()->PostDelayedTask(FROM_HERE,
      fetcher_factory_.NewRunnableMethod(&WebResourceFetcher::StartFetch),
                                         delay_ms);
  }

  // Initializes the fetching of data from the resource server.  Data
  // load calls OnURLFetchComplete.
  void StartFetch() {
    // Balanced in OnURLFetchComplete.
    web_resource_service_->AddRef();
    // First, put our next cache load on the MessageLoop.
    MessageLoop::current()->PostDelayedTask(FROM_HERE,
        fetcher_factory_.NewRunnableMethod(&WebResourceFetcher::StartFetch),
            web_resource_service_->cache_update_delay());
    // If we are still fetching data, exit.
    if (web_resource_service_->in_fetch_)
      return;
    else
      web_resource_service_->in_fetch_ = true;

    url_fetcher_.reset(new URLFetcher(GURL(
        kDefaultWebResourceServer),
        URLFetcher::GET, this));
    // Do not let url fetcher affect existing state in profile (by setting
    // cookies, for example.
    url_fetcher_->set_load_flags(net::LOAD_DISABLE_CACHE |
      net::LOAD_DO_NOT_SAVE_COOKIES);
    url_fetcher_->set_request_context(Profile::GetDefaultRequestContext());
    url_fetcher_->Start();
  }

  // From URLFetcher::Delegate.
  void OnURLFetchComplete(const URLFetcher* source,
                          const GURL& url,
                          const URLRequestStatus& status,
                          int response_code,
                          const ResponseCookies& cookies,
                          const std::string& data) {
    // Delete the URLFetcher when this function exits.
    scoped_ptr<URLFetcher> clean_up_fetcher(url_fetcher_.release());

    // Don't parse data if attempt to download was unsuccessful.
    // Stop loading new web resource data, and silently exit.
    if (!status.is_success() || (response_code != 200))
      return;

    web_resource_service_->UpdateResourceCache(data);
    web_resource_service_->Release();
  }

 private:
  // So that we can delay our start so as not to affect start-up time; also,
  // so that we can schedule future cache updates.
  ScopedRunnableMethodFactory<WebResourceFetcher> fetcher_factory_;

  // The tool that fetches the url data from the server.
  scoped_ptr<URLFetcher> url_fetcher_;

  // Our owner and creator. Ref counted.
  WebResourceService* web_resource_service_;
};

// This class coordinates a web resource unpack and parse task which is run in
// a separate process.  Results are sent back to this class and routed to
// the WebResourceService.
class WebResourceService::UnpackerClient
    : public UtilityProcessHost::Client {
 public:
  UnpackerClient(WebResourceService* web_resource_service,
                 const std::string& json_data)
    : web_resource_service_(web_resource_service),
      json_data_(json_data), got_response_(false) {
  }

  void Start() {
    AddRef();  // balanced in Cleanup.

    // If we don't have a resource_dispatcher_host_, assume we're in
    // a test and run the unpacker directly in-process.
    bool use_utility_process =
        web_resource_service_->resource_dispatcher_host_ != NULL &&
        !CommandLine::ForCurrentProcess()->HasSwitch(switches::kSingleProcess);
    if (use_utility_process) {
      BrowserThread::ID thread_id;
      CHECK(BrowserThread::GetCurrentThreadIdentifier(&thread_id));
      BrowserThread::PostTask(
          BrowserThread::IO, FROM_HERE,
          NewRunnableMethod(this, &UnpackerClient::StartProcessOnIOThread,
                            web_resource_service_->resource_dispatcher_host_,
                            thread_id));
    } else {
      WebResourceUnpacker unpacker(json_data_);
      if (unpacker.Run()) {
        OnUnpackWebResourceSucceeded(*unpacker.parsed_json());
      } else {
        OnUnpackWebResourceFailed(unpacker.error_message());
      }
    }
  }

 private:
  ~UnpackerClient() {}

  // UtilityProcessHost::Client
  virtual void OnProcessCrashed(int exit_code) {
    if (got_response_)
      return;

    OnUnpackWebResourceFailed(
        "Chrome crashed while trying to retrieve web resources.");
  }

  virtual void OnUnpackWebResourceSucceeded(
      const DictionaryValue& parsed_json) {
    web_resource_service_->OnWebResourceUnpacked(parsed_json);
    Cleanup();
  }

  virtual void OnUnpackWebResourceFailed(const std::string& error_message) {
    web_resource_service_->EndFetch();
    Cleanup();
  }

  // Release reference and set got_response_.
  void Cleanup() {
    if (got_response_)
      return;

    got_response_ = true;
    Release();
  }

  void StartProcessOnIOThread(ResourceDispatcherHost* rdh,
                              BrowserThread::ID thread_id) {
    UtilityProcessHost* host = new UtilityProcessHost(rdh, this, thread_id);
    // TODO(mrc): get proper file path when we start using web resources
    // that need to be unpacked.
    host->StartWebResourceUnpacker(json_data_);
  }

  scoped_refptr<WebResourceService> web_resource_service_;

  // Holds raw JSON string.
  const std::string& json_data_;

  // True if we got a response from the utility process and have cleaned up
  // already.
  bool got_response_;
};

// Server for dynamically loaded NTP HTML elements. TODO(mirandac): append
// locale for future usage, when we're serving localizable strings.
const char* WebResourceService::kDefaultWebResourceServer =
    "https://www.google.com/support/chrome/bin/topic/30248/inproduct";

WebResourceService::WebResourceService(Profile* profile)
    : prefs_(profile->GetPrefs()),
      profile_(profile),
      ALLOW_THIS_IN_INITIALIZER_LIST(service_factory_(this)),
      in_fetch_(false),
      web_resource_update_scheduled_(false) {
  Init();
}

WebResourceService::~WebResourceService() { }

void WebResourceService::Init() {
  cache_update_delay_ = kCacheUpdateDelay;
  resource_dispatcher_host_ = g_browser_process->resource_dispatcher_host();
  web_resource_fetcher_.reset(new WebResourceFetcher(this));
  prefs_->RegisterStringPref(prefs::kNTPWebResourceCacheUpdate, "0");
  prefs_->RegisterRealPref(prefs::kNTPCustomLogoStart, 0);
  prefs_->RegisterRealPref(prefs::kNTPCustomLogoEnd, 0);
  prefs_->RegisterRealPref(prefs::kNTPPromoStart, 0);
  prefs_->RegisterRealPref(prefs::kNTPPromoEnd, 0);
  prefs_->RegisterStringPref(prefs::kNTPPromoLine, std::string());
  prefs_->RegisterBooleanPref(prefs::kNTPPromoClosed, false);

  // If the promo start is in the future, set a notification task to invalidate
  // the NTP cache at the time of the promo start.
  double promo_start = prefs_->GetReal(prefs::kNTPPromoStart);
  double promo_end = prefs_->GetReal(prefs::kNTPPromoEnd);
  ScheduleNotification(promo_start, promo_end);
}

void WebResourceService::EndFetch() {
  in_fetch_ = false;
}

void WebResourceService::OnWebResourceUnpacked(
  const DictionaryValue& parsed_json) {
  UnpackLogoSignal(parsed_json);
  if (WebResourceServiceUtil::CanShowPromo(profile_))
    UnpackPromoSignal(parsed_json);
  EndFetch();
}

void WebResourceService::WebResourceStateChange() {
  web_resource_update_scheduled_ = false;
  NotificationService* service = NotificationService::current();
  service->Notify(NotificationType::WEB_RESOURCE_STATE_CHANGED,
                  Source<WebResourceService>(this),
                  NotificationService::NoDetails());
}

void WebResourceService::ScheduleNotification(double promo_start,
                                              double promo_end) {
  if (promo_start > 0 && promo_end > 0 && !web_resource_update_scheduled_) {
    int ms_until_start =
        static_cast<int>((base::Time::FromDoubleT(
            promo_start) - base::Time::Now()).InMilliseconds());
    int ms_until_end =
        static_cast<int>((base::Time::FromDoubleT(
            promo_end) - base::Time::Now()).InMilliseconds());
    if (ms_until_start > 0) {
      web_resource_update_scheduled_ = true;
      MessageLoop::current()->PostDelayedTask(FROM_HERE,
          service_factory_.NewRunnableMethod(
              &WebResourceService::WebResourceStateChange),
              ms_until_start);
    }
    if (ms_until_end > 0) {
      web_resource_update_scheduled_ = true;
      MessageLoop::current()->PostDelayedTask(FROM_HERE,
          service_factory_.NewRunnableMethod(
              &WebResourceService::WebResourceStateChange),
              ms_until_end);
      if (ms_until_start <= 0) {
        // Notify immediately if time is between start and end.
        WebResourceStateChange();
      }
    }
  }
}

void WebResourceService::StartAfterDelay() {
  int delay = kStartResourceFetchDelay;
  // Check whether we have ever put a value in the web resource cache;
  // if so, pull it out and see if it's time to update again.
  if (prefs_->HasPrefPath(prefs::kNTPWebResourceCacheUpdate)) {
    std::string last_update_pref =
        prefs_->GetString(prefs::kNTPWebResourceCacheUpdate);
    if (!last_update_pref.empty()) {
      double last_update_value;
      base::StringToDouble(last_update_pref, &last_update_value);
      int ms_until_update = cache_update_delay_ -
          static_cast<int>((base::Time::Now() - base::Time::FromDoubleT(
          last_update_value)).InMilliseconds());
      delay = ms_until_update > cache_update_delay_ ?
          cache_update_delay_ : (ms_until_update < kStartResourceFetchDelay ?
                                kStartResourceFetchDelay : ms_until_update);
    }
  }
  // Start fetch and wait for UpdateResourceCache.
  web_resource_fetcher_->StartAfterDelay(static_cast<int>(delay));
}

void WebResourceService::UpdateResourceCache(const std::string& json_data) {
  UnpackerClient* client = new UnpackerClient(this, json_data);
  client->Start();

  // Set cache update time in preferences.
  prefs_->SetString(prefs::kNTPWebResourceCacheUpdate,
      base::DoubleToString(base::Time::Now().ToDoubleT()));
}

void WebResourceService::UnpackTips(const DictionaryValue& parsed_json) {
  // Get dictionary of cached preferences.
  web_resource_cache_ =
      prefs_->GetMutableDictionary(prefs::kNTPWebResourceCache);

  // The list of individual tips.
  ListValue* tip_holder = new ListValue();
  web_resource_cache_->Set(WebResourceService::kTipCachePrefName, tip_holder);

  DictionaryValue* topic_dict;
  ListValue* answer_list;
  std::string topic_id;
  std::string answer_id;
  std::string inproduct;
  int tip_counter = 0;

  if (parsed_json.GetDictionary("topic", &topic_dict)) {
    if (topic_dict->GetString("topic_id", &topic_id))
      web_resource_cache_->SetString("topic_id", topic_id);
    if (topic_dict->GetList("answers", &answer_list)) {
      for (ListValue::const_iterator tip_iter = answer_list->begin();
           tip_iter != answer_list->end(); ++tip_iter) {
        if (!(*tip_iter)->IsType(Value::TYPE_DICTIONARY))
          continue;
        DictionaryValue* a_dic =
            static_cast<DictionaryValue*>(*tip_iter);
        if (a_dic->GetString("inproduct", &inproduct)) {
          tip_holder->Append(Value::CreateStringValue(inproduct));
        }
        tip_counter++;
      }
      // If tips exist, set current index to 0.
      if (!inproduct.empty()) {
        web_resource_cache_->SetInteger(
          WebResourceService::kCurrentTipPrefName, 0);
      }
    }
  }
}

void WebResourceService::UnpackPromoSignal(const DictionaryValue& parsed_json) {
  DictionaryValue* topic_dict;
  ListValue* answer_list;
  double old_promo_start = 0;
  double old_promo_end = 0;
  double promo_start = 0;
  double promo_end = 0;

  // Check for preexisting start and end values.
  if (prefs_->HasPrefPath(prefs::kNTPPromoStart) &&
      prefs_->HasPrefPath(prefs::kNTPPromoEnd)) {
    old_promo_start = prefs_->GetReal(prefs::kNTPPromoStart);
    old_promo_end = prefs_->GetReal(prefs::kNTPPromoEnd);
  }

  // Check for newly received start and end values.
  if (parsed_json.GetDictionary("topic", &topic_dict)) {
    if (topic_dict->GetList("answers", &answer_list)) {
      std::string promo_start_string = "";
      std::string promo_end_string = "";
      std::string promo_string = "";
      for (ListValue::const_iterator tip_iter = answer_list->begin();
           tip_iter != answer_list->end(); ++tip_iter) {
        if (!(*tip_iter)->IsType(Value::TYPE_DICTIONARY))
          continue;
        DictionaryValue* a_dic =
            static_cast<DictionaryValue*>(*tip_iter);
        std::string promo_signal;
        if (a_dic->GetString("name", &promo_signal)) {
          if (promo_signal == "promo_start") {
            a_dic->GetString("inproduct", &promo_start_string);
            a_dic->GetString("tooltip", &promo_string);
            prefs_->SetString(prefs::kNTPPromoLine, promo_string);
          } else if (promo_signal == "promo_end") {
            a_dic->GetString("inproduct", &promo_end_string);
          }
        }
      }
      if (!promo_start_string.empty() &&
          promo_start_string.length() > 0 &&
          !promo_end_string.empty() &&
          promo_end_string.length() > 0) {
        base::Time start_time;
        base::Time end_time;
        if (base::Time::FromString(
                ASCIIToWide(promo_start_string).c_str(), &start_time) &&
            base::Time::FromString(
                ASCIIToWide(promo_end_string).c_str(), &end_time)) {
          promo_start = start_time.ToDoubleT();
          promo_end = end_time.ToDoubleT();
        }
      }
    }
  }

  // If start or end times have changed, trigger a new web resource
  // notification, so that the logo on the NTP is updated. This check is
  // outside the reading of the web resource data, because the absence of
  // dates counts as a triggering change if there were dates before.
  if (!(old_promo_start == promo_start) ||
      !(old_promo_end == promo_end)) {
    prefs_->SetReal(prefs::kNTPPromoStart, promo_start);
    prefs_->SetReal(prefs::kNTPPromoEnd, promo_end);
    ScheduleNotification(promo_start, promo_end);
  }
}

void WebResourceService::UnpackLogoSignal(const DictionaryValue& parsed_json) {
  DictionaryValue* topic_dict;
  ListValue* answer_list;
  double old_logo_start = 0;
  double old_logo_end = 0;
  double logo_start = 0;
  double logo_end = 0;

  // Check for preexisting start and end values.
  if (prefs_->HasPrefPath(prefs::kNTPCustomLogoStart) &&
      prefs_->HasPrefPath(prefs::kNTPCustomLogoEnd)) {
    old_logo_start = prefs_->GetReal(prefs::kNTPCustomLogoStart);
    old_logo_end = prefs_->GetReal(prefs::kNTPCustomLogoEnd);
  }

  // Check for newly received start and end values.
  if (parsed_json.GetDictionary("topic", &topic_dict)) {
    if (topic_dict->GetList("answers", &answer_list)) {
      std::string logo_start_string = "";
      std::string logo_end_string = "";
      for (ListValue::const_iterator tip_iter = answer_list->begin();
           tip_iter != answer_list->end(); ++tip_iter) {
        if (!(*tip_iter)->IsType(Value::TYPE_DICTIONARY))
          continue;
        DictionaryValue* a_dic =
            static_cast<DictionaryValue*>(*tip_iter);
        std::string logo_signal;
        if (a_dic->GetString("name", &logo_signal)) {
          if (logo_signal == "custom_logo_start") {
            a_dic->GetString("inproduct", &logo_start_string);
          } else if (logo_signal == "custom_logo_end") {
            a_dic->GetString("inproduct", &logo_end_string);
          }
        }
      }
      if (!logo_start_string.empty() &&
          logo_start_string.length() > 0 &&
          !logo_end_string.empty() &&
          logo_end_string.length() > 0) {
        base::Time start_time;
        base::Time end_time;
        if (base::Time::FromString(
                ASCIIToWide(logo_start_string).c_str(), &start_time) &&
            base::Time::FromString(
                ASCIIToWide(logo_end_string).c_str(), &end_time)) {
          logo_start = start_time.ToDoubleT();
          logo_end = end_time.ToDoubleT();
        }
      }
    }
  }

  // If logo start or end times have changed, trigger a new web resource
  // notification, so that the logo on the NTP is updated. This check is
  // outside the reading of the web resource data, because the absence of
  // dates counts as a triggering change if there were dates before.
  if (!(old_logo_start == logo_start) ||
      !(old_logo_end == logo_end)) {
    prefs_->SetReal(prefs::kNTPCustomLogoStart, logo_start);
    prefs_->SetReal(prefs::kNTPCustomLogoEnd, logo_end);
    NotificationService* service = NotificationService::current();
    service->Notify(NotificationType::WEB_RESOURCE_STATE_CHANGED,
                    Source<WebResourceService>(this),
                    NotificationService::NoDetails());
  }
}

namespace WebResourceServiceUtil {

bool CanShowPromo(Profile* profile) {
  bool promo_closed = false;
  PrefService* prefs = profile->GetPrefs();
  if (prefs->HasPrefPath(prefs::kNTPPromoClosed))
    promo_closed = prefs->GetBoolean(prefs::kNTPPromoClosed);

  bool has_extensions = false;
  ExtensionService* extensions_service = profile->GetExtensionService();
  if (extensions_service) {
    const ExtensionList* extensions = extensions_service->extensions();
    for (ExtensionList::const_iterator iter = extensions->begin();
         iter != extensions->end();
         ++iter) {
      if ((*iter)->location() == Extension::INTERNAL) {
        has_extensions = true;
        break;
      }
    }
  }

  // Note that HasProfileSyncService() will be false for ChromeOS, so
  // promo_options will only be true if the user has an extension installed.
  // See http://crosbug/10209
  bool promo_options =
      (profile->HasProfileSyncService() &&
          sync_ui_util::GetStatus(
              profile->GetProfileSyncService()) == sync_ui_util::SYNCED) ||
      has_extensions;

  return !promo_closed &&
         promo_options &&
         g_browser_process->GetApplicationLocale() == "en-US";
}

}  // namespace WebResourceService

