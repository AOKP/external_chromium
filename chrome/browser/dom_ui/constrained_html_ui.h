// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DOM_UI_CONSTRAINED_HTML_UI_H_
#define CHROME_BROWSER_DOM_UI_CONSTRAINED_HTML_UI_H_
#pragma once

#include <vector>

#include "chrome/browser/dom_ui/dom_ui.h"
#include "chrome/browser/tab_contents/constrained_window.h"
#include "chrome/common/property_bag.h"

class HtmlDialogUIDelegate;
class Profile;
class RenderViewHost;
class TabContents;

class ConstrainedHtmlUIDelegate {
 public:
  virtual HtmlDialogUIDelegate* GetHtmlDialogUIDelegate() = 0;

  // Called when the dialog should close.
  virtual void OnDialogClose() = 0;
};

// ConstrainedHtmlUI is a facility to show HTML DOM_UI content
// in a tab-modal constrained dialog.  It is implemented as an adapter
// between an HtmlDialogUI object and a ConstrainedWindow object.
//
// Since ConstrainedWindow requires platform-specific delegate
// implementations, this class is just a factory stub.
class ConstrainedHtmlUI : public DOMUI {
 public:
  explicit ConstrainedHtmlUI(TabContents* contents);
  virtual ~ConstrainedHtmlUI();

  virtual void RenderViewCreated(RenderViewHost* render_view_host);

  // Create a constrained HTML dialog. The actual object that gets created
  // is a ConstrainedHtmlUIDelegate, which later triggers construction of a
  // ConstrainedHtmlUI object.
  static void CreateConstrainedHtmlDialog(Profile* profile,
                                          HtmlDialogUIDelegate* delegate,
                                          TabContents* overshadowed);

  // Returns a property accessor that can be used to set the
  // ConstrainedHtmlUIDelegate property on a TabContents.
  static PropertyAccessor<ConstrainedHtmlUIDelegate*>&
      GetPropertyAccessor();

 private:
  ConstrainedHtmlUIDelegate* GetConstrainedDelegate();

  // JS Message Handler
  void OnDialogClose(const ListValue* args);

  DISALLOW_COPY_AND_ASSIGN(ConstrainedHtmlUI);
};

#endif  // CHROME_BROWSER_DOM_UI_CONSTRAINED_HTML_UI_H_
