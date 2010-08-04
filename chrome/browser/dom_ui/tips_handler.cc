// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "app/l10n_util.h"
#include "base/callback.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/dom_ui/tips_handler.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/web_resource/web_resource_service.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/web_resource/web_resource_unpacker.h"
#include "chrome/common/url_constants.h"
#include "googleurl/src/gurl.h"
#include "grit/generated_resources.h"

DOMMessageHandler* TipsHandler::Attach(DOMUI* dom_ui) {
  dom_ui_ = dom_ui;
  tips_cache_ = dom_ui_->GetProfile()->GetPrefs()->
      GetMutableDictionary(prefs::kNTPTipsCache);
  return DOMMessageHandler::Attach(dom_ui);
}

void TipsHandler::RegisterMessages() {
  dom_ui_->RegisterMessageCallback("getTips",
      NewCallback(this, &TipsHandler::HandleGetTips));
}

void TipsHandler::HandleGetTips(const Value* content) {
  // List containing the tips to be displayed.
  ListValue list_value;

  // Holds the web resource data found in the preferences cache.
  ListValue* wr_list;

  // These values hold the data for each web resource item.
  int current_tip_index;
  std::string current_tip;

  // If tips are not correct for our language, do not send.  Wait for update.
  // We need to check here because the new tab page calls for tips before
  // the tip service starts up.
  PrefService* current_prefs = dom_ui_->GetProfile()->GetPrefs();
  if (current_prefs->HasPrefPath(prefs::kNTPTipsServer)) {
    std::string server = current_prefs->GetString(prefs::kNTPTipsServer);
    std::string locale = g_browser_process->GetApplicationLocale();
    if (!EndsWith(server, locale, false)) {
      dom_ui_->CallJavascriptFunction(L"tips", list_value);
      return;
    }
  }

  // If the user has just started using Chrome with a fresh profile, send only
  // the "Import bookmarks" promo until the user has either seen it five times
  // or added or imported bookmarks.
  if (current_prefs->GetInteger(prefs::kNTPPromoViewsRemaining) > 0) {
    SendTip(WideToUTF8(l10n_util::GetStringF(IDS_IMPORT_BOOKMARKS_PROMO,
        std::wstring(L"<button class='link'>"),
        std::wstring(L"</button>"))),
        L"set_promo_tip", 0);
    return;
  }

  if (tips_cache_ != NULL && !tips_cache_->empty()) {
    if (tips_cache_->GetInteger(
        WebResourceService::kCurrentTipPrefName, &current_tip_index) &&
        tips_cache_->GetList(
        WebResourceService::kTipCachePrefName, &wr_list) &&
        wr_list && wr_list->GetSize() > 0) {
      if (wr_list->GetSize() <= static_cast<size_t>(current_tip_index)) {
        // Check to see whether the home page is set to NTP; if not, add tip
        // to set home page before resetting tip index to 0.
        current_tip_index = 0;
        const PrefService::Preference* pref =
            dom_ui_->GetProfile()->GetPrefs()->FindPreference(
                prefs::kHomePageIsNewTabPage);
        bool value;
        if (pref && !pref->IsManaged() &&
            pref->GetValue()->GetAsBoolean(&value) && !value) {
          SendTip(WideToUTF8(l10n_util::GetString(
              IDS_NEW_TAB_MAKE_THIS_HOMEPAGE)), L"set_homepage_tip",
              current_tip_index);
          return;
        }
      }
      if (wr_list->GetString(current_tip_index, &current_tip)) {
        SendTip(current_tip, L"tip_html_text", current_tip_index + 1);
      }
    }
  }
}

void TipsHandler::SendTip(std::string tip, std::wstring tip_type,
                          int tip_index) {
  // List containing the tips to be displayed.
  ListValue list_value;
  DictionaryValue* tip_dict = new DictionaryValue();
  tip_dict->SetString(tip_type, tip);
  list_value.Append(tip_dict);
  tips_cache_->SetInteger(WebResourceService::kCurrentTipPrefName,
                          tip_index);
  // Send list of web resource items back out to the DOM.
  dom_ui_->CallJavascriptFunction(L"tips", list_value);
}

// static
void TipsHandler::RegisterUserPrefs(PrefService* prefs) {
  prefs->RegisterDictionaryPref(prefs::kNTPTipsCache);
  prefs->RegisterStringPref(prefs::kNTPTipsServer,
                            WebResourceService::kDefaultResourceServer);
}

bool TipsHandler::IsValidURL(const std::wstring& url_string) {
  GURL url(WideToUTF8(url_string));
  return !url.is_empty() && (url.SchemeIs(chrome::kHttpScheme) ||
                             url.SchemeIs(chrome::kHttpsScheme));
}
