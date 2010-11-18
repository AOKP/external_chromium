// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_VIEWS_TEXTFIELD_VIEWS_H_
#define CHROME_BROWSER_VIEWS_TEXTFIELD_VIEWS_H_
#pragma once

#include <string>

#include "chrome/browser/views/dom_view.h"

class TextfieldsUI;

class TextfieldViews : public DOMView {
 public:
  TextfieldViews();
  std::wstring GetText();
  void SetText(const std::wstring& text);

 private:
  TextfieldsUI* dom_ui();

  DISALLOW_COPY_AND_ASSIGN(TextfieldViews);
};

#endif  // CHROME_BROWSER_VIEWS_TEXTFIELD_VIEWS_H_
