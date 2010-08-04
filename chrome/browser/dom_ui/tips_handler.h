// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This class pulls data from a web resource (such as a JSON feed) which
// has been stored in the user's preferences file.  Used mainly
// by the suggestions and tips area of the new tab page.

#ifndef CHROME_BROWSER_DOM_UI_TIPS_HANDLER_H_
#define CHROME_BROWSER_DOM_UI_TIPS_HANDLER_H_

#include <string>

#include "chrome/browser/dom_ui/dom_ui.h"

class DictionaryValue;
class DOMUI;
class PrefService;
class Value;

class TipsHandler : public DOMMessageHandler {
 public:
  TipsHandler() : tips_cache_(NULL) {}
  virtual ~TipsHandler() {}

  // DOMMessageHandler implementation and overrides.
  virtual DOMMessageHandler* Attach(DOMUI* dom_ui);
  virtual void RegisterMessages();

  // Callback which pulls tips data from the preferences.
  void HandleGetTips(const Value* content);

  // Register tips cache with pref service.
  static void RegisterUserPrefs(PrefService* prefs);

 private:
  // Make sure the string we are pushing to the NTP is a valid URL.
  bool IsValidURL(const std::wstring& url_string);

  // Send a tip to the NTP.  tip_type is "tip_html_text" if the tip is from
  // the tip server, and "set_homepage_tip" if it's the tip to set the NTP
  // as home page.
  void SendTip(std::string tip, std::wstring tip_type, int tip_index);

  // So we can push data out to the page that has called this handler.
  DOMUI* dom_ui_;

  // Filled with data from cache in preferences.
  DictionaryValue* tips_cache_;

  DISALLOW_COPY_AND_ASSIGN(TipsHandler);
};

#endif  // CHROME_BROWSER_DOM_UI_TIPS_HANDLER_H_

