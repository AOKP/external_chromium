// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/offline/offline_load_page.h"

#include "app/l10n_util.h"
#include "app/resource_bundle.h"
#include "base/i18n/rtl.h"
#include "base/metrics/histogram.h"
#include "base/string_piece.h"
#include "base/stringprintf.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_list.h"
#include "chrome/browser/browser_thread.h"
#include "chrome/browser/extensions/extensions_service.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/tab_contents/navigation_controller.h"
#include "chrome/browser/tab_contents/navigation_entry.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/browser/tab_contents/tab_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/jstemplate_builder.h"
#include "chrome/common/notification_type.h"
#include "chrome/common/url_constants.h"
#include "grit/browser_resources.h"
#include "grit/generated_resources.h"

namespace {

// Maximum time to show a blank page.
const int kMaxBlankPeriod = 3000;

// This is a workaround for  crosbug.com/8285.
// Chrome sometimes fails to load the page silently
// when the load is requested right after network is
// restored. This happens more often in HTTPS than HTTP.
// This should be removed once the root cause is fixed.
const int kSecureDelayMs = 1000;
const int kDefaultDelayMs = 300;

// A utility function to set the dictionary's value given by |resource_id|.
void SetString(DictionaryValue* strings, const char* name, int resource_id) {
  strings->SetString(name, l10n_util::GetStringUTF16(resource_id));
}

}  // namespace

namespace chromeos {

// static
void OfflineLoadPage::Show(int process_host_id, int render_view_id,
                           const GURL& url, Delegate* delegate) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (NetworkStateNotifier::is_connected()) {
    // Check again in UI thread and proceed if it's connected.
    delegate->OnBlockingPageComplete(true);
  } else {
    TabContents* tab_contents =
        tab_util::GetTabContentsByID(process_host_id, render_view_id);
    DCHECK(tab_contents);
    (new OfflineLoadPage(tab_contents, url, delegate))->Show();
  }
}

OfflineLoadPage::OfflineLoadPage(TabContents* tab_contents,
                                 const GURL& url,
                                 Delegate* delegate)
    : InterstitialPage(tab_contents, true, url),
      delegate_(delegate),
      proceeded_(false),
      ALLOW_THIS_IN_INITIALIZER_LIST(method_factory_(this)),
      in_test_(false) {
  registrar_.Add(this, NotificationType::NETWORK_STATE_CHANGED,
                 NotificationService::AllSources());
}

std::string OfflineLoadPage::GetHTMLContents() {
  DictionaryValue strings;
  // Toggle Cancel button.
  strings.SetString("display_cancel",
                    tab()->controller().CanGoBack() ? "inline" : "none");

  int64 time_to_wait = std::max(
      static_cast<int64>(0),
      kMaxBlankPeriod -
      NetworkStateNotifier::GetOfflineDuration().InMilliseconds());
  // Set the timeout to show the page.
  strings.SetInteger("timeToWait", static_cast<int>(time_to_wait));
  // Button labels
  SetString(&strings, "load_button", IDS_OFFLINE_LOAD_BUTTON);
  SetString(&strings, "cancel_button", IDS_OFFLINE_CANCEL_BUTTON);

  SetString(&strings, "heading", IDS_OFFLINE_LOAD_HEADLINE);
  SetString(&strings, "network_settings", IDS_OFFLINE_NETWORK_SETTINGS);

  bool rtl = base::i18n::IsRTL();
  strings.SetString("textdirection", rtl ? "rtl" : "ltr");

  string16 failed_url(ASCIIToUTF16(url().spec()));
  if (rtl)
    base::i18n::WrapStringWithLTRFormatting(&failed_url);
  strings.SetString("url", failed_url);

  // The offline page for app has icons and slightly different message.
  Profile* profile = tab()->profile();
  DCHECK(profile);
  const Extension* extension = NULL;
  ExtensionsService* extensions_service = profile->GetExtensionsService();
  // Extension service does not exist in test.
  if (extensions_service)
    extension = extensions_service->GetExtensionByWebExtent(url());

  if (extension)
    GetAppOfflineStrings(extension, failed_url, &strings);
  else
    GetNormalOfflineStrings(failed_url, &strings);

  base::StringPiece html(
      ResourceBundle::GetSharedInstance().GetRawDataResource(
          IDR_OFFLINE_LOAD_HTML));
  return jstemplate_builder::GetI18nTemplateHtml(html, &strings);
}

void OfflineLoadPage::GetAppOfflineStrings(
    const Extension* app,
    const string16& failed_url,
    DictionaryValue* strings) const {
  strings->SetString("title", app->name());

  GURL icon_url = app->GetIconURL(Extension::EXTENSION_ICON_LARGE,
                                  ExtensionIconSet::MATCH_EXACTLY);
  if (icon_url.is_empty()) {
    strings->SetString("display_icon", "none");
    strings->SetString("icon", string16());
  } else {
    // Default icon is not accessible from interstitial page.
    // TODO(oshima): Figure out how to use default icon.
    strings->SetString("display_icon", "block");
    strings->SetString("icon", icon_url.spec());
  }

  strings->SetString(
      "msg",
      l10n_util::GetStringFUTF16(IDS_APP_OFFLINE_LOAD_DESCRIPTION,
                                 failed_url));
}

void OfflineLoadPage::GetNormalOfflineStrings(
    const string16& failed_url, DictionaryValue* strings) const {
  strings->SetString("title", tab()->GetTitle());

  // No icon for normal web site.
  strings->SetString("display_icon", "none");
  strings->SetString("icon", string16());

  strings->SetString(
      "msg",
      l10n_util::GetStringFUTF16(IDS_SITE_OFFLINE_LOAD_DESCRIPTION,
                                 failed_url));
}

void OfflineLoadPage::CommandReceived(const std::string& cmd) {
  std::string command(cmd);
  // The Jasonified response has quotes, remove them.
  if (command.length() > 1 && command[0] == '"') {
    command = command.substr(1, command.length() - 2);
  }
  // TODO(oshima): record action for metrics.
  if (command == "proceed") {
    Proceed();
  } else if (command == "dontproceed") {
    DontProceed();
  } else if (command == "open_network_settings") {
    Browser* browser = BrowserList::GetLastActive();
    DCHECK(browser);
    browser->ShowOptionsTab(chrome::kInternetOptionsSubPage);
  } else {
    LOG(WARNING) << "Unknown command:" << cmd;
  }
}

void OfflineLoadPage::Proceed() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  int delay = url().SchemeIsSecure() ? kSecureDelayMs : kDefaultDelayMs;
  if (in_test_)
    delay = 0;
  proceeded_ = true;
  BrowserThread::PostDelayedTask(
      BrowserThread::UI, FROM_HERE,
      method_factory_.NewRunnableMethod(&OfflineLoadPage::DoProceed),
      delay);
}

void OfflineLoadPage::DoProceed() {
  delegate_->OnBlockingPageComplete(true);
  InterstitialPage::Proceed();
}

void OfflineLoadPage::DontProceed() {
  // Inogre if it's already proceeded.
  if (proceeded_)
    return;
  delegate_->OnBlockingPageComplete(false);
  InterstitialPage::DontProceed();
}

void OfflineLoadPage::Observe(NotificationType type,
                              const NotificationSource& source,
                              const NotificationDetails& details) {
  if (type.value == NotificationType::NETWORK_STATE_CHANGED) {
    chromeos::NetworkStateDetails* state_details =
        Details<chromeos::NetworkStateDetails>(details).ptr();
    DVLOG(1) << "NetworkStateChanaged notification received: state="
             << state_details->state();
    if (state_details->state() ==
        chromeos::NetworkStateDetails::CONNECTED) {
      registrar_.Remove(this, NotificationType::NETWORK_STATE_CHANGED,
                        NotificationService::AllSources());
      Proceed();
    }
  } else {
    InterstitialPage::Observe(type, source, details);
  }
}

}  // namespace chromeos
