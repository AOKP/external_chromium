// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_REPOST_FORM_WARNING_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_REPOST_FORM_WARNING_VIEW_H_
#pragma once

#include "chrome/browser/tab_contents/constrained_window.h"
#include "chrome/common/notification_registrar.h"
#include "gfx/native_widget_types.h"
#include "views/window/dialog_delegate.h"

class ConstrainedWindow;
class MessageBoxView;
class NavigationController;
class RepostFormWarningController;
class TabContents;
namespace views {
class Window;
}

// Displays a dialog that warns the user that they are about to resubmit
// a form.
// To display the dialog, allocate this object on the heap. It will open the
// dialog from its constructor and then delete itself when the user dismisses
// the dialog.
class RepostFormWarningView : public ConstrainedDialogDelegate {
 public:
  // Use BrowserWindow::ShowRepostFormWarningDialog to use.
  RepostFormWarningView(gfx::NativeWindow parent_window,
                        TabContents* tab_contents);

  // views::DialogDelegate Methods:
  virtual std::wstring GetWindowTitle() const;
  virtual std::wstring GetDialogButtonLabel(
      MessageBoxFlags::DialogButton button) const;
  virtual void DeleteDelegate();

  virtual bool Cancel();
  virtual bool Accept();

  // views::WindowDelegate Methods:
  virtual views::View* GetContentsView();

 private:
  virtual ~RepostFormWarningView();

  // The message box view whose commands we handle.
  MessageBoxView* message_box_view_;

  scoped_ptr<RepostFormWarningController> controller_;

  DISALLOW_COPY_AND_ASSIGN(RepostFormWarningView);
};

#endif  // CHROME_BROWSER_UI_VIEWS_REPOST_FORM_WARNING_VIEW_H_
