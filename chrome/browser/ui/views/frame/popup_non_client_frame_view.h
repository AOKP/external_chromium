// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_POPUP_NON_CLIENT_FRAME_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_POPUP_NON_CLIENT_FRAME_VIEW_H_
#pragma once

#include "chrome/browser/views/frame/browser_non_client_frame_view.h"

class BaseTabStrip;

// BrowserNonClientFrameView implementation for popups. We let the window
// manager implementation render the decorations for popups, so this draws
// nothing.
class PopupNonClientFrameView : public BrowserNonClientFrameView {
 public:
  PopupNonClientFrameView() {}

  // NonClientFrameView:
  virtual gfx::Rect GetBoundsForClientView() const;
  virtual bool AlwaysUseCustomFrame() const;
  virtual bool AlwaysUseNativeFrame() const;
  virtual gfx::Rect GetWindowBoundsForClientBounds(
      const gfx::Rect& client_bounds) const;
  virtual int NonClientHitTest(const gfx::Point& point);
  virtual void GetWindowMask(const gfx::Size& size, gfx::Path* window_mask);
  virtual void EnableClose(bool enable);
  virtual void ResetWindowControls();

  // BrowserNonClientFrameView:
  virtual gfx::Rect GetBoundsForTabStrip(BaseTabStrip* tabstrip) const;
  virtual int GetHorizontalTabStripVerticalOffset(bool restored) const;
  virtual void UpdateThrobber(bool running);

 private:
  DISALLOW_COPY_AND_ASSIGN(PopupNonClientFrameView);
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_POPUP_NON_CLIENT_FRAME_VIEW_H_
