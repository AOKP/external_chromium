// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/views/restart_message_box.h"

#include "app/l10n_util.h"
#include "app/message_box_flags.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "views/controls/message_box_view.h"
#include "views/window/window.h"

////////////////////////////////////////////////////////////////////////////////
// RestartMessageBox, public:

// static
void RestartMessageBox::ShowMessageBox(gfx::NativeWindow parent_window) {
  // When the window closes, it will delete itself.
  new RestartMessageBox(parent_window);
}

int RestartMessageBox::GetDialogButtons() const {
  return MessageBoxFlags::DIALOGBUTTON_OK;
}

std::wstring RestartMessageBox::GetDialogButtonLabel(
    MessageBoxFlags::DialogButton button) const {
  DCHECK(button == MessageBoxFlags::DIALOGBUTTON_OK);
  return l10n_util::GetString(IDS_OK);
}

std::wstring RestartMessageBox::GetWindowTitle() const {
  return l10n_util::GetString(IDS_PRODUCT_NAME);
}

void RestartMessageBox::DeleteDelegate() {
  delete this;
}

bool RestartMessageBox::IsModal() const {
  return true;
}

views::View* RestartMessageBox::GetContentsView() {
  return message_box_view_;
}

////////////////////////////////////////////////////////////////////////////////
// RestartMessageBox, private:

RestartMessageBox::RestartMessageBox(gfx::NativeWindow parent_window) {
  const int kDialogWidth = 400;
  // Also deleted when the window closes.
  message_box_view_ = new MessageBoxView(
      MessageBoxFlags::kFlagHasMessage | MessageBoxFlags::kFlagHasOKButton,
      l10n_util::GetString(IDS_OPTIONS_RESTART_REQUIRED).c_str(),
      std::wstring(),
      kDialogWidth);
  views::Window::CreateChromeWindow(parent_window, gfx::Rect(), this)->Show();
}

RestartMessageBox::~RestartMessageBox() {
}
