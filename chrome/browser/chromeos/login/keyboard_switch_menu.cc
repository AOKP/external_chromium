// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/keyboard_switch_menu.h"

#include "app/l10n_util.h"
#include "base/i18n/rtl.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/cros/keyboard_library.h"
#include "chrome/browser/chromeos/input_method/input_method_util.h"
#include "grit/generated_resources.h"
#include "views/widget/widget_gtk.h"

namespace chromeos {

KeyboardSwitchMenu::KeyboardSwitchMenu()
    : InputMethodMenu(NULL /* pref_service */,
                      false /* is_browser_mode */,
                      false /* is_screen_locker */) {
}

////////////////////////////////////////////////////////////////////////////////
// InputMethodMenu::InputMethodMenuHost implementation.
void KeyboardSwitchMenu::UpdateUI(
    const std::wstring& name, const std::wstring& tooltip) {
  // Update all view hierarchies so that the new input method name is shown in
  // the menu button.
  views::Widget::NotifyLocaleChanged();
}

////////////////////////////////////////////////////////////////////////////////
// views::ViewMenuDelegate implementation.
void KeyboardSwitchMenu::RunMenu(views::View* source, const gfx::Point& pt) {
  PrepareForMenuOpen();
  gfx::Point new_pt(pt);
  views::MenuButton* button = static_cast<views::MenuButton*>(source);
  // Keyboard switch menu is aligned on left by default.
  int reverse_offset = button->width() + button->menu_offset().x() * 2;
  if (base::i18n::IsRTL()) {
    new_pt.set_x(pt.x() + reverse_offset);
  } else {
    new_pt.set_x(pt.x() - reverse_offset);
  }
  language_menu().RunMenuAt(new_pt, views::Menu2::ALIGN_TOPLEFT);
}

std::wstring KeyboardSwitchMenu::GetCurrentKeyboardName() const {
  const int count = GetItemCount();
  for (int i = 0; i < count; ++i) {
    if (IsItemCheckedAt(i))
      return UTF16ToWide(GetLabelAt(i));
  }
  VLOG(1) << "The input method menu is not ready yet.  Show a language name "
             "that matches the hardware keyboard layout";
  KeyboardLibrary *library = CrosLibrary::Get()->GetKeyboardLibrary();
  const std::string keyboard_layout_id =
      library->GetHardwareKeyboardLayoutName();
  const std::string language_code =
      input_method::GetLanguageCodeFromInputMethodId(keyboard_layout_id);
  return input_method::GetLanguageDisplayNameFromCode(language_code);
}

}  // namespace chromeos
