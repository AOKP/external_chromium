// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/views/tabs/side_tab.h"

#include "app/resource_bundle.h"
#include "base/utf_string_conversions.h"
#include "gfx/canvas_skia.h"
#include "gfx/favicon_size.h"
#include "gfx/path.h"
#include "gfx/skia_util.h"
#include "grit/app_resources.h"
#include "grit/theme_resources.h"
#include "views/controls/button/image_button.h"

namespace {
const int kVerticalTabHeight = 27;
const int kTitleCloseSpacing = 4;
const SkScalar kRoundRectRadius = 4;
const SkColor kTabBackgroundColor = SK_ColorWHITE;
const SkColor kTextColor = SK_ColorBLACK;

// Padding between the edge and the icon.
const int kIconLeftPadding = 5;

// Location the title starts at.
const int kTitleX = kIconLeftPadding + kFavIconSize + 5;
};

////////////////////////////////////////////////////////////////////////////////
// SideTab, public:

SideTab::SideTab(TabController* controller)
    : BaseTab(controller) {
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  close_button()->SetBackground(kTextColor,
                                rb.GetBitmapNamed(IDR_TAB_CLOSE),
                                rb.GetBitmapNamed(IDR_TAB_CLOSE_MASK));
}

SideTab::~SideTab() {
}

// static
int SideTab::GetPreferredHeight() {
  return 27;
}

////////////////////////////////////////////////////////////////////////////////
// SideTab, views::View overrides:

void SideTab::Layout() {
  if (ShouldShowIcon()) {
    int icon_x = kIconLeftPadding;
    int icon_y = (height() - kFavIconSize) / 2;
    int icon_size =
        !data().favicon.empty() ? data().favicon.width() : kFavIconSize;
    if (icon_size != kFavIconSize) {
      icon_x -= (icon_size - kFavIconSize) / 2;
      icon_y -= (icon_size - kFavIconSize) / 2;
    }
    icon_bounds_.SetRect(icon_x, icon_y, icon_size, icon_size);
  } else {
    icon_bounds_ = gfx::Rect();
  }

  gfx::Size ps = close_button()->GetPreferredSize();
  int close_y = (height() - ps.height()) / 2;
  close_button()->SetBounds(
      std::max(0, width() - ps.width() -
               (GetPreferredHeight() - ps.height()) / 2),
      close_y,
      ps.width(),
      ps.height());

  int title_y = (height() - font_height()) / 2;
  title_bounds_.SetRect(
      kTitleX,
      title_y,
      std::max(0, close_button()->x() - kTitleCloseSpacing - kTitleX),
      font_height());
}

void SideTab::Paint(gfx::Canvas* canvas) {
  if (ShouldPaintHighlight()) {
    SkPaint paint;
    paint.setColor(kTabBackgroundColor);
    paint.setAntiAlias(true);
    SkRect border_rect = { SkIntToScalar(0), SkIntToScalar(0),
                           SkIntToScalar(width()), SkIntToScalar(height()) };
    canvas->AsCanvasSkia()->drawRoundRect(border_rect,
                                          SkIntToScalar(kRoundRectRadius),
                                          SkIntToScalar(kRoundRectRadius),
                                          paint);
  }

  if (ShouldShowIcon())
    PaintIcon(canvas, icon_bounds_.x(), icon_bounds_.y());

  PaintTitle(canvas, kTextColor);
}

gfx::Size SideTab::GetPreferredSize() {
  return gfx::Size(0, GetPreferredHeight());
}

bool SideTab::ShouldPaintHighlight() const {
  return IsSelected() || !controller();
}

bool SideTab::ShouldShowIcon() const {
  return data().mini || data().show_icon;
}
