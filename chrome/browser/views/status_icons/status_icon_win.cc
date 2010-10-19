// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/views/status_icons/status_icon_win.h"

#include "base/sys_string_conversions.h"
#include "gfx/icon_util.h"
#include "gfx/point.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "views/controls/menu/menu_2.h"

StatusIconWin::StatusIconWin(UINT id, HWND window, UINT message)
    : icon_id_(id),
      window_(window),
      message_id_(message) {
  NOTIFYICONDATA icon_data;
  InitIconData(&icon_data);
  icon_data.uFlags = NIF_MESSAGE;
  icon_data.uCallbackMessage = message_id_;
  BOOL result = Shell_NotifyIcon(NIM_ADD, &icon_data);
  DCHECK(result);
}

StatusIconWin::~StatusIconWin() {
  // Remove our icon.
  NOTIFYICONDATA icon_data;
  InitIconData(&icon_data);
  Shell_NotifyIcon(NIM_DELETE, &icon_data);
}

void StatusIconWin::SetImage(const SkBitmap& image) {
  // Create the icon.
  NOTIFYICONDATA icon_data;
  InitIconData(&icon_data);
  icon_data.uFlags = NIF_ICON;
  icon_.Set(IconUtil::CreateHICONFromSkBitmap(image));
  icon_data.hIcon = icon_.Get();
  BOOL result = Shell_NotifyIcon(NIM_MODIFY, &icon_data);
  DCHECK(result);
}

void StatusIconWin::SetPressedImage(const SkBitmap& image) {
  // Ignore pressed images, since the standard on Windows is to not highlight
  // pressed status icons.
}

void StatusIconWin::SetToolTip(const string16& tool_tip) {
  // Create the icon.
  NOTIFYICONDATA icon_data;
  InitIconData(&icon_data);
  icon_data.uFlags = NIF_TIP;
  wcscpy_s(icon_data.szTip, tool_tip.c_str());
  BOOL result = Shell_NotifyIcon(NIM_MODIFY, &icon_data);
  DCHECK(result);
}

void StatusIconWin::InitIconData(NOTIFYICONDATA* icon_data) {
  icon_data->cbSize = sizeof(icon_data);
  icon_data->hWnd = window_;
  icon_data->uID = icon_id_;
}

void StatusIconWin::UpdatePlatformContextMenu(menus::MenuModel* menu) {
  // If no items are passed, blow away our context menu.
  if (!menu) {
    context_menu_.reset();
    return;
  }

  // Create context menu with the new contents.
  context_menu_.reset(new views::Menu2(menu));
}

void StatusIconWin::HandleClickEvent(int x, int y, bool left_mouse_click) {
  // Pass to the observer if appropriate.
  if (left_mouse_click && HasObservers()) {
    DispatchClickEvent();
    return;
  }

  // Event not sent to the observer, so display the context menu if one exists.
  if (context_menu_.get()) {
    // Set our window as the foreground window, so the context menu closes when
    // we click away from it.
    SetForegroundWindow(window_);
    context_menu_->RunContextMenuAt(gfx::Point(x, y));
  }
}
