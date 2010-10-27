// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_DOM_UI_REGISTER_PAGE_UI_H_
#define CHROME_BROWSER_CHROMEOS_DOM_UI_REGISTER_PAGE_UI_H_
#pragma once

#include "chrome/browser/dom_ui/dom_ui.h"

// A custom DOMUI that defines datasource for host registration page that
// is used in Chrome OS to register product on first sign in.
class RegisterPageUI : public DOMUI {
 public:
  explicit RegisterPageUI(TabContents* contents);

 private:
  DISALLOW_COPY_AND_ASSIGN(RegisterPageUI);
};

#endif  // CHROME_BROWSER_CHROMEOS_DOM_UI_REGISTER_PAGE_UI_H_
