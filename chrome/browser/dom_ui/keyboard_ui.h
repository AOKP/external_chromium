// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DOM_UI_KEYBOARD_UI_H_
#define CHROME_BROWSER_DOM_UI_KEYBOARD_UI_H_
#pragma once

#include <string>

#include "chrome/browser/dom_ui/chrome_url_data_manager.h"
#include "chrome/browser/dom_ui/web_ui.h"

class Profile;

// The TabContents used for the keyboard page.
class KeyboardUI : public WebUI {
 public:
  explicit KeyboardUI(TabContents* manager);
  ~KeyboardUI();

  class KeyboardHTMLSource : public ChromeURLDataManager::DataSource {
   public:
    KeyboardHTMLSource();

    // Overrides from DataSource
    virtual void StartDataRequest(const std::string& path,
                                  bool is_off_the_record,
                                  int request_id);
    virtual std::string GetMimeType(const std::string&) const;

   private:
    virtual ~KeyboardHTMLSource() {}

    DISALLOW_COPY_AND_ASSIGN(KeyboardHTMLSource);
  };

 private:
  DISALLOW_COPY_AND_ASSIGN(KeyboardUI);
};

#endif  // CHROME_BROWSER_DOM_UI_KEYBOARD_UI_H_
