// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_VIEWS_INFOBARS_BEFORE_TRANSLATE_INFOBAR_H_
#define CHROME_BROWSER_VIEWS_INFOBARS_BEFORE_TRANSLATE_INFOBAR_H_

#include <string>

#include "app/menus/simple_menu_model.h"
#include "chrome/browser/translate/languages_menu_model.h"
#include "chrome/browser/translate/options_menu_model.h"
#include "chrome/browser/translate/translate_infobar_view.h"
#include "chrome/browser/views/infobars/infobars.h"
#include "chrome/browser/views/infobars/translate_infobar_base.h"
#include "views/controls/button/button.h"
#include "views/controls/link.h"
#include "views/controls/menu/view_menu_delegate.h"

class InfoBarTextButton;
class TranslateInfoBarDelegate;

namespace views {
class Menu2;
class MenuButton;
}

class BeforeTranslateInfoBar
    : public TranslateInfoBarBase,
      public views::ViewMenuDelegate {
 public:
  explicit BeforeTranslateInfoBar(TranslateInfoBarDelegate* delegate);
  virtual ~BeforeTranslateInfoBar();

  // Overridden from views::View:
  virtual void Layout();

  // Overridden from views::ButtonListener:
  virtual void ButtonPressed(views::Button* sender, const views::Event& event);

  // Overridden from TranslateInfoBarView:
  virtual void OriginalLanguageChanged();
  virtual void TargetLanguageChanged();

 protected:
  // Overridden from views::ViewMenuDelegate:
  virtual void RunMenu(View* source, const gfx::Point& pt);

 private:
  // Sets the text of the original language menu button to reflect the current
  // value from the delegate.
  void UpdateOriginalButtonText();

  // The text displayed in the infobar is something like:
  // "The page is in <lang>. Would you like to translate it?"
  // Where <lang> is displayed in a combobox.
  // So the text is split in 2 chunks, each one displayed in one of the label
  // below.
  views::Label* label_1_;
  views::Label* label_2_;

  views::MenuButton* language_menu_button_;
  views::MenuButton* options_menu_button_;
  InfoBarTextButton* accept_button_;
  InfoBarTextButton* deny_button_;
  InfoBarTextButton* never_translate_button_;
  InfoBarTextButton* always_translate_button_;

  scoped_ptr<views::Menu2> languages_menu_;
  LanguagesMenuModel languages_menu_model_;

  scoped_ptr<views::Menu2> options_menu_;
  OptionsMenuModel options_menu_model_;

  DISALLOW_COPY_AND_ASSIGN(BeforeTranslateInfoBar);
};

#endif  // CHROME_BROWSER_VIEWS_INFOBARS_BEFORE_TRANSLATE_INFOBAR_H_
