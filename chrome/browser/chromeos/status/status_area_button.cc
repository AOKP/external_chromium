// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/status/status_area_button.h"

#include "gfx/canvas.h"
#include "gfx/skbitmap_operations.h"
#include "grit/theme_resources.h"
#include "views/border.h"
#include "views/view.h"

namespace chromeos {

////////////////////////////////////////////////////////////////////////////////
// StatusAreaButton

StatusAreaButton::StatusAreaButton(views::ViewMenuDelegate* menu_delegate)
    : MenuButton(NULL, std::wstring(), menu_delegate, false),
      use_menu_button_paint_(false), enabled_(true) {
  set_border(NULL);

  // Use an offset that is top aligned with toolbar.
  set_menu_offset(0, 2);
}

void StatusAreaButton::Paint(gfx::Canvas* canvas, bool for_drag) {
  if (state() == BS_PUSHED) {
    // Apply 10% white when pushed down.
    canvas->FillRectInt(SkColorSetARGB(0x19, 0xFF, 0xFF, 0xFF),
        0, 0, width(), height());
  }

  if (use_menu_button_paint_) {
    views::MenuButton::Paint(canvas, for_drag);
  } else {
    DrawIcon(canvas);
    PaintFocusBorder(canvas);
  }
}

gfx::Size StatusAreaButton::GetPreferredSize() {
  gfx::Insets insets = views::MenuButton::GetInsets();
  gfx::Size prefsize(icon_width() + insets.width(),
                     icon_height() + insets.height());

  // Adjusts size when use menu button paint.
  if (use_menu_button_paint_) {
    gfx::Size menu_button_size = views::MenuButton::GetPreferredSize();
    prefsize.SetSize(
      std::max(prefsize.width(), menu_button_size.width()),
      std::max(prefsize.height(), menu_button_size.height())
    );

    // Shift 1-pixel down for odd number of pixels in vertical space.
    if ((prefsize.height() - menu_button_size.height()) % 2) {
      insets_.Set(insets.top() + 1, insets.left(),
          insets.bottom(), insets.right());
    }
  }

  // Add padding.
  prefsize.Enlarge(2 * horizontal_padding(), 0);

  return prefsize;
}

gfx::Insets StatusAreaButton::GetInsets() const {
  return insets_;
}

void StatusAreaButton::SetText(const std::wstring& text) {
  // TextButtons normally remember the max text size, so the button's preferred
  // size will always be as large as the largest text ever put in it.
  // We clear that max text size, so we can adjust the size to fit the text.
  ClearMaxTextSize();
  views::MenuButton::SetText(text);
}

void StatusAreaButton::DrawIcon(gfx::Canvas* canvas) {
  canvas->DrawBitmapInt(icon(), horizontal_padding(), 0);
}

bool StatusAreaButton::Activate() {
  if (enabled_) {
    return views::MenuButton::Activate();
  } else {
    return true;
  }
}

}  // namespace chromeos
