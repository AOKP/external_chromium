// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/views/sad_tab_view.h"

#include "app/l10n_util.h"
#include "app/resource_bundle.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "gfx/canvas.h"
#include "gfx/canvas_skia.h"
#include "gfx/size.h"
#include "gfx/skia_util.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "grit/theme_resources.h"
#include "third_party/skia/include/effects/SkGradientShader.h"

static const int kSadTabOffset = -64;
static const int kIconTitleSpacing = 20;
static const int kTitleMessageSpacing = 15;
static const int kMessageBottomMargin = 20;
static const float kMessageSize = 0.65f;
static const SkColor kTitleColor = SK_ColorWHITE;
static const SkColor kMessageColor = SK_ColorWHITE;
static const SkColor kLinkColor = SK_ColorWHITE;
static const SkColor kBackgroundColor = SkColorSetRGB(35, 48, 64);
static const SkColor kBackgroundEndColor = SkColorSetRGB(35, 48, 64);

// static
SkBitmap* SadTabView::sad_tab_bitmap_ = NULL;
gfx::Font* SadTabView::title_font_ = NULL;
gfx::Font* SadTabView::message_font_ = NULL;
std::wstring SadTabView::title_;
std::wstring SadTabView::message_;
int SadTabView::title_width_;

SadTabView::SadTabView(TabContents* tab_contents)
    : tab_contents_(tab_contents),
      learn_more_link_(NULL) {
  DCHECK(tab_contents);

  InitClass();

  if (tab_contents != NULL) {
    learn_more_link_ = new views::Link(l10n_util::GetString(IDS_LEARN_MORE));
    learn_more_link_->SetFont(*message_font_);
    learn_more_link_->SetNormalColor(kLinkColor);
    learn_more_link_->SetController(this);
    AddChildView(learn_more_link_);
  }
}

void SadTabView::Paint(gfx::Canvas* canvas) {
  SkPaint paint;
  paint.setShader(gfx::CreateGradientShader(0, height(),
                                            kBackgroundColor,
                                            kBackgroundEndColor))->safeUnref();
  paint.setStyle(SkPaint::kFill_Style);
  canvas->AsCanvasSkia()->drawRectCoords(
      0, 0, SkIntToScalar(width()), SkIntToScalar(height()), paint);

  canvas->DrawBitmapInt(*sad_tab_bitmap_, icon_bounds_.x(), icon_bounds_.y());

  canvas->DrawStringInt(title_, *title_font_, kTitleColor, title_bounds_.x(),
                        title_bounds_.y(), title_bounds_.width(),
                        title_bounds_.height(),
                        gfx::Canvas::TEXT_ALIGN_CENTER);

  canvas->DrawStringInt(message_, *message_font_, kMessageColor,
                        message_bounds_.x(), message_bounds_.y(),
                        message_bounds_.width(), message_bounds_.height(),
                        gfx::Canvas::MULTI_LINE);

  if (learn_more_link_ != NULL)
    learn_more_link_->SetBounds(link_bounds_.x(), link_bounds_.y(),
                                link_bounds_.width(), link_bounds_.height());
}

void SadTabView::Layout() {
  int icon_width = sad_tab_bitmap_->width();
  int icon_height = sad_tab_bitmap_->height();
  int icon_x = (width() - icon_width) / 2;
  int icon_y = ((height() - icon_height) / 2) + kSadTabOffset;
  icon_bounds_.SetRect(icon_x, icon_y, icon_width, icon_height);

  int title_x = (width() - title_width_) / 2;
  int title_y = icon_bounds_.bottom() + kIconTitleSpacing;
  int title_height = title_font_->height();
  title_bounds_.SetRect(title_x, title_y, title_width_, title_height);

  gfx::CanvasSkia cc(0, 0, true);
  int message_width = static_cast<int>(width() * kMessageSize);
  int message_height = 0;
  cc.SizeStringInt(message_, *message_font_, &message_width, &message_height,
                   gfx::Canvas::MULTI_LINE);
  int message_x = (width() - message_width) / 2;
  int message_y = title_bounds_.bottom() + kTitleMessageSpacing;
  message_bounds_.SetRect(message_x, message_y, message_width, message_height);

  if (learn_more_link_ != NULL) {
    gfx::Size sz = learn_more_link_->GetPreferredSize();
    gfx::Insets insets = learn_more_link_->GetInsets();
    link_bounds_.SetRect((width() - sz.width()) / 2,
                         message_bounds_.bottom() + kTitleMessageSpacing -
                         insets.top(), sz.width(), sz.height());
  }
}

void SadTabView::LinkActivated(views::Link* source, int event_flags) {
  if (tab_contents_ != NULL && source == learn_more_link_) {
    string16 url = l10n_util::GetStringUTF16(IDS_CRASH_REASON_URL);
    tab_contents_->OpenURL(GURL(url), GURL(), CURRENT_TAB,
        PageTransition::LINK);
  }
}

// static
void SadTabView::InitClass() {
  static bool initialized = false;
  if (!initialized) {
    ResourceBundle& rb = ResourceBundle::GetSharedInstance();
    title_font_ = new gfx::Font(
        rb.GetFont(ResourceBundle::BaseFont).DeriveFont(2, gfx::Font::BOLD));
    message_font_ = new gfx::Font(
        rb.GetFont(ResourceBundle::BaseFont).DeriveFont(1));
    sad_tab_bitmap_ = rb.GetBitmapNamed(IDR_SAD_TAB);

    title_ = l10n_util::GetString(IDS_SAD_TAB_TITLE);
    title_width_ = title_font_->GetStringWidth(title_);
    message_ = l10n_util::GetString(IDS_SAD_TAB_MESSAGE);

    initialized = true;
  }
}
