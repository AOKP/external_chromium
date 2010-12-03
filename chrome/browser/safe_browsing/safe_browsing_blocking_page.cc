// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Implementation of the SafeBrowsingBlockingPage class.

#include "chrome/browser/safe_browsing/safe_browsing_blocking_page.h"

#include <string>

#include "app/l10n_util.h"
#include "app/resource_bundle.h"
#include "base/i18n/rtl.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_thread.h"
#include "chrome/browser/dom_operation_notification_details.h"
#include "chrome/browser/dom_ui/new_tab_ui.h"
#include "chrome/browser/google/google_util.h"
#include "chrome/browser/metrics/user_metrics.h"
#include "chrome/browser/safe_browsing/safe_browsing_service.h"
#include "chrome/browser/tab_contents/navigation_controller.h"
#include "chrome/browser/tab_contents/navigation_entry.h"
#include "chrome/browser/tab_contents/tab_util.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/common/jstemplate_builder.h"
#include "chrome/common/url_constants.h"
#include "grit/browser_resources.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "net/base/escape.h"

// For malware interstitial pages, we link the problematic URL to Google's
// diagnostic page.
#if defined(GOOGLE_CHROME_BUILD)
static const char* const kSbDiagnosticUrl =
    "http://safebrowsing.clients.google.com/safebrowsing/diagnostic?site=%s&client=googlechrome";
#else
static const char* const kSbDiagnosticUrl =
    "http://safebrowsing.clients.google.com/safebrowsing/diagnostic?site=%s&client=chromium";
#endif

static const char* const kSbReportPhishingUrl =
    "http://www.google.com/safebrowsing/report_error/";

// URL for the "Learn more" link on the multi threat malware blocking page.
static const char* const kLearnMoreMalwareUrl =
    "http://www.google.com/support/bin/answer.py?answer=45449&topic=360"
    "&sa=X&oi=malwarewarninglink&resnum=1&ct=help";

// URL for the "Learn more" link on the phishing blocking page.
static const char* const kLearnMorePhishingUrl =
    "http://www.google.com/support/bin/answer.py?answer=106318";

static const wchar_t* const kSbDiagnosticHtml =
    L"<a href=\"\" onclick=\"sendCommand('showDiagnostic'); return false;\" "
    L"onmousedown=\"return false;\">%ls</a>";

static const wchar_t* const kPLinkHtml =
    L"<a href=\"\" onclick=\"sendCommand('proceed'); return false;\" "
    L"onmousedown=\"return false;\">%ls</a>";

// The commands returned by the page when the user performs an action.
static const char* const kShowDiagnosticCommand = "showDiagnostic";
static const char* const kReportErrorCommand = "reportError";
static const char* const kLearnMoreCommand = "learnMore";
static const char* const kProceedCommand = "proceed";
static const char* const kTakeMeBackCommand = "takeMeBack";

// static
SafeBrowsingBlockingPageFactory* SafeBrowsingBlockingPage::factory_ = NULL;

// The default SafeBrowsingBlockingPageFactory.  Global, made a singleton so we
// don't leak it.
class SafeBrowsingBlockingPageFactoryImpl
    : public SafeBrowsingBlockingPageFactory {
 public:
  SafeBrowsingBlockingPage* CreateSafeBrowsingPage(
      SafeBrowsingService* service,
      TabContents* tab_contents,
      const SafeBrowsingBlockingPage::UnsafeResourceList& unsafe_resources) {
    return new SafeBrowsingBlockingPage(service, tab_contents,
                                        unsafe_resources);
  }

 private:
  friend struct DefaultSingletonTraits<SafeBrowsingBlockingPageFactoryImpl>;

  SafeBrowsingBlockingPageFactoryImpl() { }

  DISALLOW_COPY_AND_ASSIGN(SafeBrowsingBlockingPageFactoryImpl);
};

SafeBrowsingBlockingPage::SafeBrowsingBlockingPage(
    SafeBrowsingService* sb_service,
    TabContents* tab_contents,
    const UnsafeResourceList& unsafe_resources)
    : InterstitialPage(tab_contents,
                       IsMainPage(unsafe_resources),
                       unsafe_resources[0].url),
      sb_service_(sb_service),
      is_main_frame_(IsMainPage(unsafe_resources)),
      unsafe_resources_(unsafe_resources) {
  RecordUserAction(SHOW);
  if (!is_main_frame_) {
    navigation_entry_index_to_remove_ =
        tab()->controller().last_committed_entry_index();
  } else {
    navigation_entry_index_to_remove_ = -1;
  }
}

SafeBrowsingBlockingPage::~SafeBrowsingBlockingPage() {
}

std::string SafeBrowsingBlockingPage::GetHTMLContents() {
  // Load the HTML page and create the template components.
  DictionaryValue strings;
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  std::string html;

  if (unsafe_resources_.empty()) {
    NOTREACHED();
    return std::string();
  }

  if (unsafe_resources_.size() > 1) {
    PopulateMultipleThreatStringDictionary(&strings);
    html = rb.GetRawDataResource(
        IDR_SAFE_BROWSING_MULTIPLE_THREAT_BLOCK).as_string();
  } else if (unsafe_resources_[0].threat_type ==
             SafeBrowsingService::URL_MALWARE) {
    PopulateMalwareStringDictionary(&strings);
    html = rb.GetRawDataResource(IDR_SAFE_BROWSING_MALWARE_BLOCK).as_string();
  } else {  // Phishing.
    DCHECK(unsafe_resources_[0].threat_type ==
           SafeBrowsingService::URL_PHISHING);
    PopulatePhishingStringDictionary(&strings);
    html = rb.GetRawDataResource(IDR_SAFE_BROWSING_PHISHING_BLOCK).as_string();
  }

  return jstemplate_builder::GetTemplatesHtml(html, &strings, "template_root");
}

void SafeBrowsingBlockingPage::PopulateStringDictionary(
    DictionaryValue* strings,
    const std::wstring& title,
    const std::wstring& headline,
    const std::wstring& description1,
    const std::wstring& description2,
    const std::wstring& description3) {
  strings->SetString("title", WideToUTF16Hack(title));
  strings->SetString("headLine", WideToUTF16Hack(headline));
  strings->SetString("description1", WideToUTF16Hack(description1));
  strings->SetString("description2", WideToUTF16Hack(description2));
  strings->SetString("description3", WideToUTF16Hack(description3));
}

void SafeBrowsingBlockingPage::PopulateMultipleThreatStringDictionary(
    DictionaryValue* strings) {
  bool malware = false;
  bool phishing = false;

  string16 malware_label =
      l10n_util::GetStringUTF16(IDS_SAFE_BROWSING_MALWARE_LABEL);
  string16 malware_link =
      l10n_util::GetStringUTF16(IDS_SAFE_BROWSING_MALWARE_DIAGNOSTIC_PAGE);
  string16 phishing_label =
      l10n_util::GetStringUTF16(IDS_SAFE_BROWSING_PHISHING_LABEL);
  string16 phishing_link =
      l10n_util::GetStringUTF16(IDS_SAFE_BROWSING_PHISHING_REPORT_ERROR);

  ListValue* error_strings = new ListValue;
  for (UnsafeResourceList::const_iterator iter = unsafe_resources_.begin();
       iter != unsafe_resources_.end(); ++iter) {
    const SafeBrowsingService::UnsafeResource& resource = *iter;
    DictionaryValue* current_error_strings = new DictionaryValue;
    if (resource.threat_type == SafeBrowsingService::URL_MALWARE) {
      malware = true;
      current_error_strings->SetString("type", "malware");
      current_error_strings->SetString("typeLabel", malware_label);
      current_error_strings->SetString("errorLink", malware_link);
    } else {
      DCHECK(resource.threat_type == SafeBrowsingService::URL_PHISHING);
      phishing = true;
      current_error_strings->SetString("type", "phishing");
      current_error_strings->SetString("typeLabel", phishing_label);
      current_error_strings->SetString("errorLink", phishing_link);
    }
    current_error_strings->SetString("url", resource.url.spec());
    error_strings->Append(current_error_strings);
  }
  strings->Set("errors", error_strings);
  DCHECK(phishing || malware);

  if (malware && phishing) {
    PopulateStringDictionary(
        strings,
        // Use the malware headline, it is the scariest one.
        l10n_util::GetString(IDS_SAFE_BROWSING_MULTI_THREAT_TITLE),
        l10n_util::GetString(IDS_SAFE_BROWSING_MALWARE_HEADLINE),
        l10n_util::GetStringF(IDS_SAFE_BROWSING_MULTI_THREAT_DESCRIPTION1,
                              UTF8ToWide(tab()->GetURL().host())),
        l10n_util::GetString(IDS_SAFE_BROWSING_MULTI_THREAT_DESCRIPTION2),
        L"");
  } else if (malware) {
    // Just malware.
    PopulateStringDictionary(
        strings,
        l10n_util::GetString(IDS_SAFE_BROWSING_MALWARE_TITLE),
        l10n_util::GetString(IDS_SAFE_BROWSING_MALWARE_HEADLINE),
        l10n_util::GetStringF(IDS_SAFE_BROWSING_MULTI_MALWARE_DESCRIPTION1,
                              UTF8ToWide(tab()->GetURL().host())),
        l10n_util::GetString(IDS_SAFE_BROWSING_MULTI_MALWARE_DESCRIPTION2),
        l10n_util::GetString(IDS_SAFE_BROWSING_MULTI_MALWARE_DESCRIPTION3));
  } else {
    // Just phishing.
    PopulateStringDictionary(
        strings,
        l10n_util::GetString(IDS_SAFE_BROWSING_PHISHING_TITLE),
        l10n_util::GetString(IDS_SAFE_BROWSING_PHISHING_HEADLINE),
        l10n_util::GetStringF(IDS_SAFE_BROWSING_MULTI_PHISHING_DESCRIPTION1,
                              UTF8ToWide(tab()->GetURL().host())),
        L"", L"");
  }

  strings->SetString("confirm_text",
                     l10n_util::GetStringUTF16(
                         IDS_SAFE_BROWSING_MULTI_MALWARE_DESCRIPTION_AGREE));
  strings->SetString("continue_button",
                     l10n_util::GetStringUTF16(
                         IDS_SAFE_BROWSING_MULTI_MALWARE_PROCEED_BUTTON));
  strings->SetString("back_button",
      l10n_util::GetStringUTF16(IDS_SAFE_BROWSING_MALWARE_BACK_BUTTON));
  strings->SetString("textdirection", base::i18n::IsRTL() ? "rtl" : "ltr");
}

void SafeBrowsingBlockingPage::PopulateMalwareStringDictionary(
    DictionaryValue* strings) {
  std::wstring diagnostic_link = StringPrintf(kSbDiagnosticHtml,
      l10n_util::GetString(IDS_SAFE_BROWSING_MALWARE_DIAGNOSTIC_PAGE).c_str());

  strings->SetString("badURL", url().host());
  // Check to see if we're blocking the main page, or a sub-resource on the
  // main page.
  std::wstring description1, description3, description5;
  if (is_main_frame_) {
    description1 = l10n_util::GetStringF(IDS_SAFE_BROWSING_MALWARE_DESCRIPTION1,
                                         UTF8ToWide(url().host()));
  } else {
    description1 = l10n_util::GetStringF(IDS_SAFE_BROWSING_MALWARE_DESCRIPTION4,
                                         UTF8ToWide(tab()->GetURL().host()),
                                         UTF8ToWide(url().host()));
  }

  std::wstring proceed_link = StringPrintf(kPLinkHtml,
      l10n_util::GetString(IDS_SAFE_BROWSING_MALWARE_PROCEED_LINK).c_str());
  description3 = l10n_util::GetStringF(IDS_SAFE_BROWSING_MALWARE_DESCRIPTION3,
                                       proceed_link);

  PopulateStringDictionary(
      strings,
      l10n_util::GetString(IDS_SAFE_BROWSING_MALWARE_TITLE),
      l10n_util::GetString(IDS_SAFE_BROWSING_MALWARE_HEADLINE),
      description1,
      l10n_util::GetString(IDS_SAFE_BROWSING_MALWARE_DESCRIPTION2),
      description3);

  description5 = l10n_util::GetStringF(IDS_SAFE_BROWSING_MALWARE_DESCRIPTION5,
                                       UTF8ToWide(url().host()),
                                       UTF8ToWide(url().host()),
                                       diagnostic_link);

  strings->SetString("description5", WideToUTF16Hack(description5));

  strings->SetString("back_button",
      l10n_util::GetStringUTF16(IDS_SAFE_BROWSING_MALWARE_BACK_BUTTON));
  strings->SetString("more_info_button",
      l10n_util::GetStringUTF16(IDS_SAFE_BROWSING_MALWARE_MORE_INFO_BUTTON));
  strings->SetString("less_info_button",
      l10n_util::GetStringUTF16(IDS_SAFE_BROWSING_MALWARE_LESS_INFO_BUTTON));
  strings->SetString("proceed_link",
      l10n_util::GetStringUTF16(IDS_SAFE_BROWSING_MALWARE_PROCEED_LINK));
  strings->SetString("textdirection", base::i18n::IsRTL() ? "rtl" : "ltr");
}

void SafeBrowsingBlockingPage::PopulatePhishingStringDictionary(
    DictionaryValue* strings) {
  PopulateStringDictionary(
      strings,
      l10n_util::GetString(IDS_SAFE_BROWSING_PHISHING_TITLE),
      l10n_util::GetString(IDS_SAFE_BROWSING_PHISHING_HEADLINE),
      l10n_util::GetStringF(IDS_SAFE_BROWSING_PHISHING_DESCRIPTION1,
                            UTF8ToWide(url().host())),
      l10n_util::GetStringF(IDS_SAFE_BROWSING_PHISHING_DESCRIPTION2,
                            UTF8ToWide(url().host())),
      L"");

  strings->SetString("continue_button",
      l10n_util::GetStringUTF16(IDS_SAFE_BROWSING_PHISHING_PROCEED_BUTTON));
  strings->SetString("back_button",
      l10n_util::GetStringUTF16(IDS_SAFE_BROWSING_PHISHING_BACK_BUTTON));
  strings->SetString("report_error",
      l10n_util::GetStringUTF16(IDS_SAFE_BROWSING_PHISHING_REPORT_ERROR));
  strings->SetString("textdirection", base::i18n::IsRTL() ? "rtl" : "ltr");
}

void SafeBrowsingBlockingPage::CommandReceived(const std::string& cmd) {
  std::string command(cmd);  // Make a local copy so we can modify it.
  // The Jasonified response has quotes, remove them.
  if (command.length() > 1 && command[0] == '"') {
    command = command.substr(1, command.length() - 2);
  }

  if (command == kLearnMoreCommand) {
    // User pressed "Learn more".
    GURL url;
    if (unsafe_resources_[0].threat_type == SafeBrowsingService::URL_MALWARE) {
      url = google_util::AppendGoogleLocaleParam(GURL(kLearnMoreMalwareUrl));
    } else if (unsafe_resources_[0].threat_type ==
               SafeBrowsingService::URL_PHISHING) {
      url = google_util::AppendGoogleLocaleParam(GURL(kLearnMorePhishingUrl));
    } else {
      NOTREACHED();
    }
    tab()->OpenURL(url, GURL(), CURRENT_TAB, PageTransition::LINK);
    return;
  }

  if (command == kProceedCommand) {
    Proceed();
    // We are deleted after this.
    return;
  }

  if (command == kTakeMeBackCommand) {
    DontProceed();
    // We are deleted after this.
    return;
  }

  // The "report error" and "show diagnostic" commands can have a number
  // appended to them, which is the index of the element they apply to.
  int element_index = 0;
  size_t colon_index = command.find(':');
  if (colon_index != std::string::npos) {
    DCHECK(colon_index < command.size() - 1);
    bool result = base::StringToInt(command.begin() + colon_index + 1,
                                    command.end(),
                                    &element_index);
    command = command.substr(0, colon_index);
    DCHECK(result);
  }

  if (element_index >= static_cast<int>(unsafe_resources_.size())) {
    NOTREACHED();
    return;
  }

  std::string bad_url_spec = unsafe_resources_[element_index].url.spec();
  if (command == kReportErrorCommand) {
    // User pressed "Report error" for a phishing site.
    // Note that we cannot just put a link in the interstitial at this point.
    // It is not OK to navigate in the context of an interstitial page.
    DCHECK(unsafe_resources_[element_index].threat_type ==
           SafeBrowsingService::URL_PHISHING);
    GURL report_url =
        safe_browsing_util::GeneratePhishingReportUrl(kSbReportPhishingUrl,
                                                      bad_url_spec);
    tab()->OpenURL(report_url, GURL(), CURRENT_TAB, PageTransition::LINK);
    return;
  }

  if (command == kShowDiagnosticCommand) {
    // We're going to take the user to Google's SafeBrowsing diagnostic page.
    std::string diagnostic =
        StringPrintf(kSbDiagnosticUrl,
                     EscapeQueryParamValue(bad_url_spec, true).c_str());
    GURL diagnostic_url(diagnostic);
    diagnostic_url = google_util::AppendGoogleLocaleParam(diagnostic_url);
    DCHECK(unsafe_resources_[element_index].threat_type ==
           SafeBrowsingService::URL_MALWARE);
    tab()->OpenURL(diagnostic_url, GURL(), CURRENT_TAB, PageTransition::LINK);
    return;
  }

  NOTREACHED() << "Unexpected command: " << command;
}

void SafeBrowsingBlockingPage::Proceed() {
  RecordUserAction(PROCEED);

  NotifySafeBrowsingService(sb_service_, unsafe_resources_, true);

  // Check to see if some new notifications of unsafe resources have been
  // received while we were showing the interstitial.
  UnsafeResourceMap* unsafe_resource_map = GetUnsafeResourcesMap();
  UnsafeResourceMap::iterator iter = unsafe_resource_map->find(tab());
  SafeBrowsingBlockingPage* blocking_page = NULL;
  if (iter != unsafe_resource_map->end() && !iter->second.empty()) {
    // Build an interstitial for all the unsafe resources notifications.
    // Don't show it now as showing an interstitial while an interstitial is
    // already showing would cause DontProceed() to be invoked.
    blocking_page = factory_->CreateSafeBrowsingPage(sb_service_, tab(),
                                                     iter->second);
    unsafe_resource_map->erase(iter);
  }

  InterstitialPage::Proceed();
  // We are now deleted.

  // Now that this interstitial is gone, we can show the new one.
  if (blocking_page)
    blocking_page->Show();
}

void SafeBrowsingBlockingPage::DontProceed() {
  DCHECK(action_taken() != DONT_PROCEED_ACTION);
  // We could have already called Proceed(), in which case we must not notify
  // the SafeBrowsingService again, as the client has been deleted.
  if (action_taken() == PROCEED_ACTION) {
    // We still want to hide the interstitial page.
    InterstitialPage::DontProceed();
    // We are now deleted.
    return;
  }

  RecordUserAction(DONT_PROCEED);

  NotifySafeBrowsingService(sb_service_, unsafe_resources_, false);

  // The user does not want to proceed, clear the queued unsafe resources
  // notifications we received while the interstitial was showing.
  UnsafeResourceMap* unsafe_resource_map = GetUnsafeResourcesMap();
  UnsafeResourceMap::iterator iter = unsafe_resource_map->find(tab());
  if (iter != unsafe_resource_map->end() && !iter->second.empty()) {
    NotifySafeBrowsingService(sb_service_, iter->second, false);
    unsafe_resource_map->erase(iter);
  }

  // We don't remove the navigation entry if the tab is being destroyed as this
  // would trigger a navigation that would cause trouble as the render view host
  // for the tab has by then already been destroyed.
  if (navigation_entry_index_to_remove_ != -1 && !tab()->is_being_destroyed()) {
    tab()->controller().RemoveEntryAtIndex(navigation_entry_index_to_remove_,
                                           GURL(chrome::kChromeUINewTabURL));
    navigation_entry_index_to_remove_ = -1;
  }
  InterstitialPage::DontProceed();
  // We are now deleted.
}

void SafeBrowsingBlockingPage::RecordUserAction(BlockingPageEvent event) {
  // Determine the interstitial type from the blocked resources.
  // This is the same logic that is used to actually construct the
  // page contents; we can look at the title to see which type of
  // interstitial is being displayed.
  DictionaryValue strings;
  PopulateMultipleThreatStringDictionary(&strings);

  string16 title;
  DCHECK(strings.GetString("title", &title));

  std::string action = "SBInterstitial";
  if (title ==
          l10n_util::GetStringUTF16(IDS_SAFE_BROWSING_MULTI_THREAT_TITLE)) {
    action.append("Multiple");
  } else if (title ==
                 l10n_util::GetStringUTF16(IDS_SAFE_BROWSING_MALWARE_TITLE)) {
    action.append("Malware");
  } else {
    DCHECK_EQ(title,
              l10n_util::GetStringUTF16(IDS_SAFE_BROWSING_PHISHING_TITLE));
    action.append("Phishing");
  }

  switch (event) {
    case SHOW:
      action.append("Show");
      break;
    case PROCEED:
      action.append("Proceed");
      break;
    case DONT_PROCEED:
      action.append("DontProceed");
      break;
    default:
      NOTREACHED() << "Unexpected event: " << event;
  }

  UserMetrics::RecordComputedAction(action);
}

// static
void SafeBrowsingBlockingPage::NotifySafeBrowsingService(
    SafeBrowsingService* sb_service,
    const UnsafeResourceList& unsafe_resources,
    bool proceed) {
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      NewRunnableMethod(
          sb_service, &SafeBrowsingService::OnBlockingPageDone,
          unsafe_resources, proceed));
}

// static
SafeBrowsingBlockingPage::UnsafeResourceMap*
    SafeBrowsingBlockingPage::GetUnsafeResourcesMap() {
  return Singleton<UnsafeResourceMap>::get();
}

// static
void SafeBrowsingBlockingPage::ShowBlockingPage(
    SafeBrowsingService* sb_service,
    const SafeBrowsingService::UnsafeResource& unsafe_resource) {
  TabContents* tab_contents = tab_util::GetTabContentsByID(
      unsafe_resource.render_process_host_id, unsafe_resource.render_view_id);

  InterstitialPage* interstitial =
      InterstitialPage::GetInterstitialPage(tab_contents);
  if (interstitial &&
      unsafe_resource.resource_type == ResourceType::MAIN_FRAME) {
    // There is already an interstitial showing and we are about to display a
    // new one for the main frame. Just hide the current one, it is now
    // irrelevent
    interstitial->DontProceed();
    interstitial = NULL;
  }

  if (!interstitial) {
    // There are no interstitial currently showing in that tab, go ahead and
    // show this interstitial.
    std::vector<SafeBrowsingService::UnsafeResource> resources;
    resources.push_back(unsafe_resource);
    // Set up the factory if this has not been done already (tests do that
    // before this method is called).
    if (!factory_)
      factory_ = Singleton<SafeBrowsingBlockingPageFactoryImpl>::get();
    SafeBrowsingBlockingPage* blocking_page =
        factory_->CreateSafeBrowsingPage(sb_service, tab_contents, resources);
    blocking_page->Show();
    return;
  }

  // This is an interstitial for a page's resource, let's queue it.
  UnsafeResourceMap* unsafe_resource_map = GetUnsafeResourcesMap();
  (*unsafe_resource_map)[tab_contents].push_back(unsafe_resource);
}

// static
bool SafeBrowsingBlockingPage::IsMainPage(
    const UnsafeResourceList& unsafe_resources) {
  return unsafe_resources.size() == 1 &&
         unsafe_resources[0].resource_type == ResourceType::MAIN_FRAME;
}
