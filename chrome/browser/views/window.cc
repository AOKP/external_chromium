// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/views/window.h"

#include "gfx/rect.h"
#include "views/window/window.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/frame/bubble_window.h"
#endif  // defined(OS_CHROMEOS)

namespace browser {

views::Window* CreateViewsWindow(gfx::NativeWindow parent,
                                 const gfx::Rect& bounds,
                                 views::WindowDelegate* delegate) {
#if defined(OS_CHROMEOS)
  return chromeos::BubbleWindow::Create(parent, gfx::Rect(), delegate);
#else
  return views::Window::CreateChromeWindow(parent, gfx::Rect(), delegate);
#endif
}

}  // namespace browser
