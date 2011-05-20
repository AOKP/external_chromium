// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/views/theme_background.h"

#include "app/resource_bundle.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/themes/browser_theme_provider.h"
#include "chrome/browser/views/frame/browser_view.h"
#include "gfx/canvas.h"
#include "grit/app_resources.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "views/view.h"

ThemeBackground::ThemeBackground(BrowserView* browser_view)
    : browser_view_(browser_view) {
}

void ThemeBackground::Paint(gfx::Canvas* canvas, views::View* view) const {
  SkBitmap* background;

  // Never theme app and popup windows.
  if (!browser_view_->IsBrowserTypeNormal()) {
    ResourceBundle& rb = ResourceBundle::GetSharedInstance();
    if (browser_view_->IsActive())
      background = rb.GetBitmapNamed(IDR_FRAME);
    else
      background = rb.GetBitmapNamed(IDR_THEME_FRAME_INACTIVE);
  } else {
    Profile* profile = browser_view_->browser()->profile();
    ThemeProvider* theme = profile->GetThemeProvider();
    if (browser_view_->IsActive()) {
      background = theme->GetBitmapNamed(
          profile->IsOffTheRecord() ?
          IDR_THEME_FRAME_INCOGNITO : IDR_THEME_FRAME);
    } else {
      background = theme->GetBitmapNamed(
          profile->IsOffTheRecord() ?
          IDR_THEME_FRAME_INCOGNITO_INACTIVE : IDR_THEME_FRAME_INACTIVE);
    }
  }

  gfx::Point origin(0, 0);
  views::View::ConvertPointToView(view,
                                  browser_view_->frame()->GetFrameView(),
                                  &origin);
#if defined(OS_CHROMEOS)
  const int kCustomFrameBackgroundVerticalOffset = 15;
  // TODO(oshima): Remove this once we fully migrated to views.
  // See http://crbug.com/28580.
  if (browser_view_->IsMaximized()) {
    origin.Offset(0, kCustomFrameBackgroundVerticalOffset + 1);
  }
#endif
  canvas->TileImageInt(*background,
                       origin.x(), origin.y(), 0, 0,
                       view->width(), view->height());
}
