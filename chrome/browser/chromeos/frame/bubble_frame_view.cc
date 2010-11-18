// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/frame/bubble_frame_view.h"

#include "app/resource_bundle.h"
#include "gfx/canvas_skia.h"
#include "gfx/font.h"
#include "gfx/insets.h"
#include "gfx/path.h"
#include "gfx/rect.h"
#include "chrome/browser/chromeos/frame/bubble_window.h"
#include "chrome/browser/views/bubble_border.h"
#include "grit/theme_resources.h"
#include "views/controls/button/image_button.h"
#include "views/controls/label.h"
#include "views/window/hit_test.h"
#include "views/window/window.h"
#include "views/window/window_delegate.h"
#include "third_party/skia/include/core/SkPaint.h"

namespace {

static const int kTitleTopPadding = 10;
static const int kTitleContentPadding = 10;
static const int kHorizontalPadding = 10;

}  // namespace

namespace chromeos {

BubbleFrameView::BubbleFrameView(views::Window* frame,
                                 BubbleWindow::Style style)
    : frame_(frame),
      style_(style),
      title_(NULL),
      close_button_(NULL) {
  set_border(new BubbleBorder(BubbleBorder::NONE));

  if (frame_->GetDelegate()->ShouldShowWindowTitle()) {
    title_ = new views::Label(frame_->GetDelegate()->GetWindowTitle());
    title_->SetHorizontalAlignment(views::Label::ALIGN_LEFT);
    title_->SetFont(title_->font().DeriveFont(1, gfx::Font::BOLD));
    AddChildView(title_);
  }

  if (style_ & BubbleWindow::STYLE_XBAR) {
    ResourceBundle& rb = ResourceBundle::GetSharedInstance();
    close_button_ = new views::ImageButton(this);
    close_button_->SetImage(views::CustomButton::BS_NORMAL,
        rb.GetBitmapNamed(IDR_CLOSE_BAR));
    close_button_->SetImage(views::CustomButton::BS_HOT,
        rb.GetBitmapNamed(IDR_CLOSE_BAR_H));
    close_button_->SetImage(views::CustomButton::BS_PUSHED,
        rb.GetBitmapNamed(IDR_CLOSE_BAR_P));
    AddChildView(close_button_);
  }
}

BubbleFrameView::~BubbleFrameView() {
}

gfx::Rect BubbleFrameView::GetBoundsForClientView() const {
  return client_view_bounds_;
}

gfx::Rect BubbleFrameView::GetWindowBoundsForClientBounds(
    const gfx::Rect& client_bounds) const {
  gfx::Insets insets = GetInsets();
  gfx::Size title_size;
  if (title_) {
    title_size = title_->GetPreferredSize();
  }

  gfx::Size close_button_size = gfx::Size();
  if (close_button_) {
    close_button_size = close_button_->GetPreferredSize();
  }

  int top_height = insets.top();
  if (title_size.height() > 0 || close_button_size.height() > 0)
    top_height += std::max(title_size.height(), close_button_size.height()) +
        kTitleContentPadding;
  return gfx::Rect(std::max(0, client_bounds.x() - insets.left()),
                   std::max(0, client_bounds.y() - top_height),
                   client_bounds.width() + insets.width(),
                   client_bounds.height() + top_height + insets.bottom());
}

int BubbleFrameView::NonClientHitTest(const gfx::Point& point) {
  return HTNOWHERE;
}

void BubbleFrameView::GetWindowMask(const gfx::Size& size,
                                    gfx::Path* window_mask) {
}

void BubbleFrameView::EnableClose(bool enable) {
}

void BubbleFrameView::ResetWindowControls() {
}

gfx::Insets BubbleFrameView::GetInsets() const {
  gfx::Insets border_insets;
  border()->GetInsets(&border_insets);

  gfx::Insets insets(kTitleTopPadding,
                     kHorizontalPadding,
                     0,
                     kHorizontalPadding);
  insets += border_insets;
  return insets;
}

gfx::Size BubbleFrameView::GetPreferredSize() {
  gfx::Size pref = frame_->GetClientView()->GetPreferredSize();
  gfx::Rect bounds(0, 0, pref.width(), pref.height());
  return frame_->GetNonClientView()->GetWindowBoundsForClientBounds(
      bounds).size();
}

void BubbleFrameView::Layout() {
  gfx::Insets insets = GetInsets();

  gfx::Size title_size;
  if (title_) {
    title_size = title_->GetPreferredSize();
  }

  gfx::Size close_button_size = gfx::Size();
  if (close_button_) {
    close_button_size = close_button_->GetPreferredSize();
  }

  if (title_) {
    title_->SetBounds(
        insets.left(), insets.top(),
        std::max(0, width() - insets.width() - close_button_size.width()),
        title_size.height());
  }

  if (close_button_) {
    close_button_->SetBounds(
        width() - insets.right() - close_button_size.width(), insets.top(),
        close_button_size.width(), close_button_size.height());
  }

  int top_height = insets.top();
  if (title_size.height() > 0 || close_button_size.height() > 0)
    top_height += std::max(title_size.height(), close_button_size.height()) +
        kTitleContentPadding;
  client_view_bounds_.SetRect(insets.left(), top_height,
      std::max(0, width() - insets.width()),
      std::max(0, height() - top_height - insets.bottom()));
}

void BubbleFrameView::Paint(gfx::Canvas* canvas) {
  // The border of this view creates an anti-aliased round-rect region for the
  // contents, which we need to fill with the background color.
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setStyle(SkPaint::kFill_Style);
  paint.setColor(BubbleWindow::kBackgroundColor);
  gfx::Path path;
  gfx::Rect bounds(GetLocalBounds(false));
  SkRect rect;
  rect.set(SkIntToScalar(bounds.x()), SkIntToScalar(bounds.y()),
           SkIntToScalar(bounds.right()), SkIntToScalar(bounds.bottom()));
  SkScalar radius = SkIntToScalar(BubbleBorder::GetCornerRadius());
  path.addRoundRect(rect, radius, radius);
  canvas->AsCanvasSkia()->drawPath(path, paint);

  PaintBorder(canvas);
}

void BubbleFrameView::ButtonPressed(views::Button* sender,
                                    const views::Event& event) {
  if (close_button_ != NULL && sender == close_button_) {
    frame_->Close();
  }
}

}  // namespace chromeos
