// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/glue/webthemeengine_impl_win.h"

#include "gfx/native_theme_win.h"
#include "skia/ext/platform_canvas.h"
#include "skia/ext/skia_utils_win.h"
#include "third_party/WebKit/WebKit/chromium/public/WebRect.h"

using WebKit::WebCanvas;
using WebKit::WebColor;
using WebKit::WebRect;

namespace webkit_glue {

static RECT WebRectToRECT(const WebRect& rect) {
  RECT result;
  result.left = rect.x;
  result.top = rect.y;
  result.right = rect.x + rect.width;
  result.bottom = rect.y + rect.height;
  return result;
}

void WebThemeEngineImpl::paintButton(
    WebCanvas* canvas, int part, int state, int classic_state,
    const WebRect& rect) {
  HDC hdc = canvas->beginPlatformPaint();

  RECT native_rect = WebRectToRECT(rect);
  gfx::NativeTheme::instance()->PaintButton(
      hdc, part, state, classic_state, &native_rect);

  canvas->endPlatformPaint();
}

void WebThemeEngineImpl::paintMenuList(
    WebCanvas* canvas, int part, int state, int classic_state,
    const WebRect& rect) {
  HDC hdc = canvas->beginPlatformPaint();

  RECT native_rect = WebRectToRECT(rect);
  gfx::NativeTheme::instance()->PaintMenuList(
      hdc, part, state, classic_state, &native_rect);

  canvas->endPlatformPaint();
}

void WebThemeEngineImpl::paintScrollbarArrow(
    WebCanvas* canvas, int state, int classic_state,
    const WebRect& rect) {
  HDC hdc = canvas->beginPlatformPaint();

  RECT native_rect = WebRectToRECT(rect);
  gfx::NativeTheme::instance()->PaintScrollbarArrow(
      hdc, state, classic_state, &native_rect);

  canvas->endPlatformPaint();
}

void WebThemeEngineImpl::paintScrollbarThumb(
    WebCanvas* canvas, int part, int state, int classic_state,
    const WebRect& rect) {
  HDC hdc = canvas->beginPlatformPaint();

  RECT native_rect = WebRectToRECT(rect);
  gfx::NativeTheme::instance()->PaintScrollbarThumb(
      hdc, part, state, classic_state, &native_rect);

  canvas->endPlatformPaint();
}

void WebThemeEngineImpl::paintScrollbarTrack(
    WebCanvas* canvas, int part, int state, int classic_state,
    const WebRect& rect, const WebRect& align_rect) {
  HDC hdc = canvas->beginPlatformPaint();

  RECT native_rect = WebRectToRECT(rect);
  RECT native_align_rect = WebRectToRECT(align_rect);
  gfx::NativeTheme::instance()->PaintScrollbarTrack(
      hdc, part, state, classic_state, &native_rect, &native_align_rect,
      canvas);

  canvas->endPlatformPaint();
}

void WebThemeEngineImpl::paintSpinButton(
    WebCanvas* canvas, int part, int state, int classic_state,
    const WebRect& rect) {
  HDC hdc = canvas->beginPlatformPaint();

  RECT native_rect = WebRectToRECT(rect);
  gfx::NativeTheme::instance()->PaintSpinButton(
      hdc, part, state, classic_state, &native_rect);

  canvas->endPlatformPaint();
}

void WebThemeEngineImpl::paintTextField(
    WebCanvas* canvas, int part, int state, int classic_state,
    const WebRect& rect, WebColor color, bool fill_content_area,
    bool draw_edges) {
  HDC hdc = canvas->beginPlatformPaint();

  RECT native_rect = WebRectToRECT(rect);
  COLORREF c = skia::SkColorToCOLORREF(color);

  gfx::NativeTheme::instance()->PaintTextField(
      hdc, part, state, classic_state, &native_rect, c, fill_content_area,
      draw_edges);

  canvas->endPlatformPaint();
}

void WebThemeEngineImpl::paintTrackbar(
    WebCanvas* canvas, int part, int state, int classic_state,
    const WebRect& rect) {
  HDC hdc = canvas->beginPlatformPaint();

  RECT native_rect = WebRectToRECT(rect);
  gfx::NativeTheme::instance()->PaintTrackbar(
      hdc, part, state, classic_state, &native_rect, canvas);

  canvas->endPlatformPaint();
}

void WebThemeEngineImpl::paintProgressBar(
    WebCanvas* canvas, const WebRect& barRect, const WebRect& valueRect,
    bool determinate, double animatedSeconds)
{
  HDC hdc = canvas->beginPlatformPaint();
  RECT native_bar_rect = WebRectToRECT(barRect);
  RECT native_value_rect = WebRectToRECT(valueRect);
  gfx::NativeTheme::instance()->PaintProgressBar(
      hdc, &native_bar_rect,
      &native_value_rect, determinate, animatedSeconds, canvas);
  canvas->endPlatformPaint();
}

}  // namespace webkit_glue
