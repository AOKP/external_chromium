// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/views/status_icons/status_tray_win.h"

#include "base/win_util.h"
#include "chrome/browser/views/status_icons/status_icon_win.h"
#include "chrome/common/chrome_constants.h"

static const UINT kStatusIconMessage = WM_APP + 1;

StatusTrayWin::StatusTrayWin()
    : next_icon_id_(1) {
  // Register our window class
  HINSTANCE hinst = GetModuleHandle(NULL);
  WNDCLASSEX wc = {0};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = StatusTrayWin::WndProcStatic;
  wc.hInstance = hinst;
  wc.lpszClassName = chrome::kStatusTrayWindowClass;
  ATOM clazz = RegisterClassEx(&wc);
  DCHECK(clazz);

  // Create an offscreen window for handling messages for the status icons.
  window_ = CreateWindow(chrome::kStatusTrayWindowClass,
                         0, 0, 0, 0, 0, 0, HWND_MESSAGE, 0, hinst, 0);
  win_util::SetWindowUserData(window_, this);
}

LRESULT CALLBACK StatusTrayWin::WndProcStatic(HWND hwnd,
                                              UINT message,
                                              WPARAM wparam,
                                              LPARAM lparam) {
  StatusTrayWin* msg_wnd = reinterpret_cast<StatusTrayWin*>(
      GetWindowLongPtr(hwnd, GWLP_USERDATA));
  return msg_wnd->WndProc(hwnd, message, wparam, lparam);
}

LRESULT CALLBACK StatusTrayWin::WndProc(HWND hwnd,
                                        UINT message,
                                        WPARAM wparam,
                                        LPARAM lparam) {
  // TODO(atwilson): Add support for right clicks and context menu messages
  // (tracked in http://crbug.com/37375).
  switch (message) {
    case kStatusIconMessage:
      switch (lparam) {
        case WM_LBUTTONDOWN:
          // Walk our icons, find which one was clicked on, and invoke its
          // DispatchClickEvent() method.
          for (StatusIconMap::const_iterator iter = status_icons().begin();
               iter != status_icons().end();
               ++iter) {
            StatusIconWin* win_icon =
                static_cast<StatusIconWin*>(iter->second);
            if (win_icon->icon_id() == wparam)
              win_icon->DispatchClickEvent();
          }
          return TRUE;
      }
      break;
  }
  return ::DefWindowProc(hwnd, message, wparam, lparam);
}

StatusTrayWin::~StatusTrayWin() {
  if (window_)
    DestroyWindow(window_);
  UnregisterClass(chrome::kStatusTrayWindowClass, GetModuleHandle(NULL));
}

StatusIcon* StatusTrayWin::CreateStatusIcon() {
  return new StatusIconWin(next_icon_id_++, window_, kStatusIconMessage);
}

StatusTray* StatusTray::Create() {
  return new StatusTrayWin();
}
