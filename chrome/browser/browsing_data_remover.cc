// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/browsing_data_remover.h"

#include <map>
#include <set>

#include "base/callback.h"
#include "chrome/browser/chrome_thread.h"
#include "chrome/browser/download/download_manager.h"
#include "chrome/browser/extensions/extensions_service.h"
#include "chrome/browser/history/history.h"
#include "chrome/browser/in_process_webkit/webkit_context.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/metrics/user_metrics.h"
#include "chrome/browser/net/chrome_url_request_context.h"
#include "chrome/browser/password_manager/password_store.h"
#include "chrome/browser/search_engines/template_url_model.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/tab_restore_service.h"
#include "chrome/browser/webdata/web_data_service.h"
#include "chrome/common/net/url_request_context_getter.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/url_constants.h"
#include "net/base/cookie_monster.h"
#include "net/base/net_errors.h"
#include "net/base/transport_security_state.h"
#include "net/disk_cache/disk_cache.h"
#include "net/http/http_cache.h"
#include "net/url_request/url_request_context.h"
#include "webkit/database/database_tracker.h"
#include "webkit/database/database_util.h"

// Done so that we can use PostTask on BrowsingDataRemovers and not have
// BrowsingDataRemover implement RefCounted.
DISABLE_RUNNABLE_METHOD_REFCOUNT(BrowsingDataRemover);

bool BrowsingDataRemover::removing_ = false;

BrowsingDataRemover::BrowsingDataRemover(Profile* profile,
                                         base::Time delete_begin,
                                         base::Time delete_end)
    : profile_(profile),
      delete_begin_(delete_begin),
      delete_end_(delete_end),
      ALLOW_THIS_IN_INITIALIZER_LIST(database_cleared_callback_(
          this, &BrowsingDataRemover::OnClearedDatabases)),
      ALLOW_THIS_IN_INITIALIZER_LIST(cache_callback_(
          this, &BrowsingDataRemover::DoClearCache)),
      ALLOW_THIS_IN_INITIALIZER_LIST(appcache_got_info_callback_(
          this, &BrowsingDataRemover::OnGotAppCacheInfo)),
      ALLOW_THIS_IN_INITIALIZER_LIST(appcache_deleted_callback_(
          this, &BrowsingDataRemover::OnAppCacheDeleted)),
      request_context_getter_(profile->GetRequestContext()),
      appcaches_to_be_deleted_count_(0),
      next_cache_state_(STATE_NONE),
      cache_(NULL),
      waiting_for_clear_databases_(false),
      waiting_for_clear_history_(false),
      waiting_for_clear_cache_(false),
      waiting_for_clear_appcache_(false) {
  DCHECK(profile);
}

BrowsingDataRemover::BrowsingDataRemover(Profile* profile,
                                         TimePeriod time_period,
                                         base::Time delete_end)
    : profile_(profile),
      delete_begin_(CalculateBeginDeleteTime(time_period)),
      delete_end_(delete_end),
      ALLOW_THIS_IN_INITIALIZER_LIST(database_cleared_callback_(
          this, &BrowsingDataRemover::OnClearedDatabases)),
      ALLOW_THIS_IN_INITIALIZER_LIST(cache_callback_(
          this, &BrowsingDataRemover::DoClearCache)),
      ALLOW_THIS_IN_INITIALIZER_LIST(appcache_got_info_callback_(
          this, &BrowsingDataRemover::OnGotAppCacheInfo)),
      ALLOW_THIS_IN_INITIALIZER_LIST(appcache_deleted_callback_(
          this, &BrowsingDataRemover::OnAppCacheDeleted)),
      request_context_getter_(profile->GetRequestContext()),
      appcaches_to_be_deleted_count_(0),
      next_cache_state_(STATE_NONE),
      cache_(NULL),
      waiting_for_clear_databases_(false),
      waiting_for_clear_history_(false),
      waiting_for_clear_cache_(false),
      waiting_for_clear_appcache_(false) {
  DCHECK(profile);
}

BrowsingDataRemover::~BrowsingDataRemover() {
  DCHECK(all_done());
}

void BrowsingDataRemover::Remove(int remove_mask) {
  DCHECK(!removing_);
  removing_ = true;

  std::vector<GURL> origin_whitelist;
  ExtensionsService* extensions_service = profile_->GetExtensionsService();
  if (extensions_service && extensions_service->HasInstalledExtensions()) {
    std::map<GURL, int> whitelist_map =
        extensions_service->protected_storage_map();
    for (std::map<GURL, int>::const_iterator iter = whitelist_map.begin();
         iter != whitelist_map.end(); ++iter) {
      origin_whitelist.push_back(iter->first);
    }
  }

  std::vector<string16> webkit_db_whitelist;
  for (size_t i = 0; i < origin_whitelist.size(); ++i) {
    webkit_db_whitelist.push_back(
        webkit_database::DatabaseUtil::GetOriginIdentifier(
            origin_whitelist[i]));
  }


  if (remove_mask & REMOVE_HISTORY) {
    HistoryService* history_service =
        profile_->GetHistoryService(Profile::EXPLICIT_ACCESS);
    if (history_service) {
      std::set<GURL> restrict_urls;
      UserMetrics::RecordAction(UserMetricsAction("ClearBrowsingData_History"),
                                profile_);
      waiting_for_clear_history_ = true;
      history_service->ExpireHistoryBetween(restrict_urls,
          delete_begin_, delete_end_,
          &request_consumer_,
          NewCallback(this, &BrowsingDataRemover::OnHistoryDeletionDone));
    }

    // As part of history deletion we also delete the auto-generated keywords.
    TemplateURLModel* keywords_model = profile_->GetTemplateURLModel();
    if (keywords_model && !keywords_model->loaded()) {
      registrar_.Add(this, NotificationType::TEMPLATE_URL_MODEL_LOADED,
                     Source<TemplateURLModel>(keywords_model));
      keywords_model->Load();
    } else if (keywords_model) {
      keywords_model->RemoveAutoGeneratedBetween(delete_begin_, delete_end_);
    }

    // We also delete the list of recently closed tabs. Since these expire,
    // they can't be more than a day old, so we can simply clear them all.
    TabRestoreService* tab_service = profile_->GetTabRestoreService();
    if (tab_service) {
      tab_service->ClearEntries();
      tab_service->DeleteLastSession();
    }

    // We also delete the last session when we delete the history.
    SessionService* session_service = profile_->GetSessionService();
    if (session_service)
      session_service->DeleteLastSession();
  }

  if (remove_mask & REMOVE_DOWNLOADS) {
    UserMetrics::RecordAction(UserMetricsAction("ClearBrowsingData_Downloads"),
                              profile_);
    DownloadManager* download_manager = profile_->GetDownloadManager();
    download_manager->RemoveDownloadsBetween(delete_begin_, delete_end_);
    download_manager->ClearLastDownloadPath();
  }

  if (remove_mask & REMOVE_COOKIES) {
    UserMetrics::RecordAction(UserMetricsAction("ClearBrowsingData_Cookies"),
                              profile_);
    // Since we are running on the UI thread don't call GetURLRequestContext().
    net::CookieMonster* cookie_monster =
        profile_->GetRequestContext()->GetCookieStore()->GetCookieMonster();
    if (cookie_monster)
      cookie_monster->DeleteAllCreatedBetween(delete_begin_, delete_end_, true);

    // REMOVE_COOKIES is actually "cookies and other site data" so we make sure
    // to remove other data such local databases, STS state, etc.
    profile_->GetWebKitContext()->DeleteDataModifiedSince(
        delete_begin_, chrome::kExtensionScheme, webkit_db_whitelist);

    database_tracker_ = profile_->GetDatabaseTracker();
    if (database_tracker_.get()) {
      waiting_for_clear_databases_ = true;
      ChromeThread::PostTask(
          ChromeThread::FILE, FROM_HERE,
          NewRunnableMethod(
              this,
              &BrowsingDataRemover::ClearDatabasesOnFILEThread,
              delete_begin_,
              webkit_db_whitelist));
    }

    ChromeThread::PostTask(
        ChromeThread::IO, FROM_HERE,
        NewRunnableMethod(
            profile_->GetTransportSecurityState(),
            &net::TransportSecurityState::DeleteSince,
            delete_begin_));

    waiting_for_clear_appcache_ = true;
    ChromeThread::PostTask(
        ChromeThread::IO, FROM_HERE,
        NewRunnableMethod(
            this,
            &BrowsingDataRemover::ClearAppCacheOnIOThread,
            delete_begin_,  // we assume end time == now
            origin_whitelist));
  }

  if (remove_mask & REMOVE_PASSWORDS) {
    UserMetrics::RecordAction(UserMetricsAction("ClearBrowsingData_Passwords"),
                              profile_);
    PasswordStore* password_store =
        profile_->GetPasswordStore(Profile::EXPLICIT_ACCESS);

    if (password_store)
      password_store->RemoveLoginsCreatedBetween(delete_begin_, delete_end_);
  }

  if (remove_mask & REMOVE_FORM_DATA) {
    UserMetrics::RecordAction(UserMetricsAction("ClearBrowsingData_Autofill"),
                              profile_);
    WebDataService* web_data_service =
        profile_->GetWebDataService(Profile::EXPLICIT_ACCESS);

    if (web_data_service) {
      web_data_service->RemoveFormElementsAddedBetween(delete_begin_,
          delete_end_);
    }
  }

  if (remove_mask & REMOVE_CACHE) {
    // Invoke ClearBrowsingDataView::ClearCache on the IO thread.
    waiting_for_clear_cache_ = true;
    UserMetrics::RecordAction(UserMetricsAction("ClearBrowsingData_Cache"),
                              profile_);

    main_context_getter_ = profile_->GetRequestContext();
    media_context_getter_ = profile_->GetRequestContextForMedia();

    ChromeThread::PostTask(
        ChromeThread::IO, FROM_HERE,
        NewRunnableMethod(this, &BrowsingDataRemover::ClearCacheOnIOThread));
  }

  NotifyAndDeleteIfDone();
}

void BrowsingDataRemover::AddObserver(Observer* observer) {
  observer_list_.AddObserver(observer);
}

void BrowsingDataRemover::RemoveObserver(Observer* observer) {
  observer_list_.RemoveObserver(observer);
}

void BrowsingDataRemover::OnHistoryDeletionDone() {
  waiting_for_clear_history_ = false;
  NotifyAndDeleteIfDone();
}

base::Time BrowsingDataRemover::CalculateBeginDeleteTime(
    TimePeriod time_period) {
  base::TimeDelta diff;
  base::Time delete_begin_time = base::Time::Now();
  switch (time_period) {
    case LAST_HOUR:
      diff = base::TimeDelta::FromHours(1);
      break;
    case LAST_DAY:
      diff = base::TimeDelta::FromHours(24);
      break;
    case LAST_WEEK:
      diff = base::TimeDelta::FromHours(7*24);
      break;
    case FOUR_WEEKS:
      diff = base::TimeDelta::FromHours(4*7*24);
      break;
    case EVERYTHING:
      delete_begin_time = base::Time();
      break;
    default:
      NOTREACHED() << L"Missing item";
      break;
  }
  return delete_begin_time - diff;
}

void BrowsingDataRemover::Observe(NotificationType type,
                                  const NotificationSource& source,
                                  const NotificationDetails& details) {
  // TODO(brettw) bug 1139736: This should also observe session
  // clearing (what about other things such as passwords, etc.?) and wait for
  // them to complete before continuing.
  DCHECK(type == NotificationType::TEMPLATE_URL_MODEL_LOADED);
  TemplateURLModel* model = Source<TemplateURLModel>(source).ptr();
  if (model->profile() == profile_->GetOriginalProfile()) {
    registrar_.RemoveAll();
    model->RemoveAutoGeneratedBetween(delete_begin_, delete_end_);
    NotifyAndDeleteIfDone();
  }
}

void BrowsingDataRemover::NotifyAndDeleteIfDone() {
  // TODO(brettw) bug 1139736: see TODO in Observe() above.
  if (!all_done())
    return;

  removing_ = false;
  FOR_EACH_OBSERVER(Observer, observer_list_, OnBrowsingDataRemoverDone());

  // History requests aren't happy if you delete yourself from the callback.
  // As such, we do a delete later.
  MessageLoop::current()->DeleteSoon(FROM_HERE, this);
}

void BrowsingDataRemover::ClearedCache() {
  waiting_for_clear_cache_ = false;

  NotifyAndDeleteIfDone();
}

void BrowsingDataRemover::ClearCacheOnIOThread() {
  // This function should be called on the IO thread.
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::IO));
  DCHECK_EQ(STATE_NONE, next_cache_state_);
  DCHECK(main_context_getter_);
  DCHECK(media_context_getter_);

  next_cache_state_ = STATE_CREATE_MAIN;
  DoClearCache(net::OK);
}

// The expected state sequence is STATE_NONE --> STATE_CREATE_MAIN -->
// STATE_DELETE_MAIN --> STATE_CREATE_MEDIA --> STATE_DELETE_MEDIA -->
// STATE_DONE, and any errors are ignored.
void BrowsingDataRemover::DoClearCache(int rv) {
  DCHECK_NE(STATE_NONE, next_cache_state_);

  while (rv != net::ERR_IO_PENDING && next_cache_state_ != STATE_NONE) {
    switch (next_cache_state_) {
      case STATE_CREATE_MAIN:
      case STATE_CREATE_MEDIA: {
        // Get a pointer to the cache.
        URLRequestContextGetter* getter =
            (next_cache_state_ == STATE_CREATE_MAIN) ?
                main_context_getter_ : media_context_getter_;
        net::HttpTransactionFactory* factory =
            getter->GetURLRequestContext()->http_transaction_factory();

        rv = factory->GetCache()->GetBackend(&cache_, &cache_callback_);
        next_cache_state_ = (next_cache_state_ == STATE_CREATE_MAIN) ?
                                STATE_DELETE_MAIN : STATE_DELETE_MEDIA;
        break;
      }
      case STATE_DELETE_MAIN:
      case STATE_DELETE_MEDIA: {
        // |cache_| can be null if it cannot be initialized.
        if (cache_) {
          if (delete_begin_.is_null()) {
            rv = cache_->DoomAllEntries(&cache_callback_);
          } else {
            rv = cache_->DoomEntriesBetween(delete_begin_, delete_end_,
                                            &cache_callback_);
          }
          cache_ = NULL;
        }
        next_cache_state_ = (next_cache_state_ == STATE_DELETE_MAIN) ?
                                STATE_CREATE_MEDIA : STATE_DONE;
        break;
      }
      case STATE_DONE: {
        main_context_getter_ = NULL;
        media_context_getter_ = NULL;
        cache_ = NULL;

        // Notify the UI thread that we are done.
        ChromeThread::PostTask(
            ChromeThread::UI, FROM_HERE,
            NewRunnableMethod(this, &BrowsingDataRemover::ClearedCache));

        next_cache_state_ = STATE_NONE;
        break;
      }
      default: {
        NOTREACHED() << "bad state";
        next_cache_state_ = STATE_NONE;  // Stop looping.
        break;
      }
    }
  }
}

void BrowsingDataRemover::OnClearedDatabases(int rv) {
  if (!ChromeThread::CurrentlyOn(ChromeThread::UI)) {
    bool result = ChromeThread::PostTask(
        ChromeThread::UI, FROM_HERE,
        NewRunnableMethod(this, &BrowsingDataRemover::OnClearedDatabases, rv));
    DCHECK(result);
    return;
  }
  // Notify the UI thread that we are done.
  database_tracker_ = NULL;
  waiting_for_clear_databases_ = false;

  NotifyAndDeleteIfDone();
}

void BrowsingDataRemover::ClearDatabasesOnFILEThread(base::Time delete_begin,
    const std::vector<string16>& webkit_db_whitelist) {
  // This function should be called on the FILE thread.
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::FILE));

  int rv = database_tracker_->DeleteDataModifiedSince(
      delete_begin, webkit_db_whitelist, &database_cleared_callback_);
  if (rv != net::ERR_IO_PENDING)
    OnClearedDatabases(rv);
}

void BrowsingDataRemover::OnClearedAppCache() {
  if (!ChromeThread::CurrentlyOn(ChromeThread::UI)) {
    bool result = ChromeThread::PostTask(
        ChromeThread::UI, FROM_HERE,
        NewRunnableMethod(this, &BrowsingDataRemover::OnClearedAppCache));
    DCHECK(result);
    return;
  }
  appcache_whitelist_.clear();
  waiting_for_clear_appcache_ = false;
  NotifyAndDeleteIfDone();
}

void BrowsingDataRemover::ClearAppCacheOnIOThread(base::Time delete_begin,
    const std::vector<GURL>& origin_whitelist) {
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::IO));
  DCHECK(waiting_for_clear_appcache_);

  appcache_whitelist_ = origin_whitelist;
  appcache_info_ = new appcache::AppCacheInfoCollection;
  GetAppCacheService()->GetAllAppCacheInfo(
      appcache_info_, &appcache_got_info_callback_);
  // continues in OnGotAppCacheInfo
}

void BrowsingDataRemover::OnGotAppCacheInfo(int rv) {
  using appcache::AppCacheInfoVector;
  typedef std::map<GURL, AppCacheInfoVector> InfoByOrigin;

  for (InfoByOrigin::const_iterator origin =
           appcache_info_->infos_by_origin.begin();
       origin != appcache_info_->infos_by_origin.end(); ++origin) {

    bool found_in_whitelist = false;
    for (size_t i = 0; i < appcache_whitelist_.size(); ++i) {
      if (appcache_whitelist_[i] == origin->first)
        found_in_whitelist = true;
    }
    if (found_in_whitelist)
      continue;

    for (AppCacheInfoVector::const_iterator info = origin->second.begin();
         info != origin->second.end(); ++info) {
      if (info->creation_time > delete_begin_) {
        ++appcaches_to_be_deleted_count_;
        GetAppCacheService()->DeleteAppCacheGroup(
            info->manifest_url, &appcache_deleted_callback_);
      }
    }
  }

  if (!appcaches_to_be_deleted_count_)
    OnClearedAppCache();
  // else continues in OnAppCacheDeleted
}

void BrowsingDataRemover::OnAppCacheDeleted(int rv) {
  --appcaches_to_be_deleted_count_;
  if (!appcaches_to_be_deleted_count_)
    OnClearedAppCache();
}

ChromeAppCacheService* BrowsingDataRemover::GetAppCacheService() {
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::IO));
  ChromeURLRequestContext* request_context =
      reinterpret_cast<ChromeURLRequestContext*>(
          request_context_getter_->GetURLRequestContext());
  return request_context ? request_context->appcache_service()
                         : NULL;
}
