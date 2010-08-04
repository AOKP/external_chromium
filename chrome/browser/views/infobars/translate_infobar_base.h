// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_VIEWS_INFOBARS_TRANSLATE_INFOBAR_BASE_H_
#define CHROME_BROWSER_VIEWS_INFOBARS_TRANSLATE_INFOBAR_BASE_H_

#include "chrome/browser/translate/translate_infobar_view.h"
#include "chrome/browser/views/infobars/infobars.h"

class TranslateInfoBarDelegate;

namespace views {
class MenuButton;
class ViewMenuDelegate;
}

// This class contains some of the base functionality that translate infobars
// use.
class TranslateInfoBarBase : public TranslateInfoBarView,
                             public InfoBar {
 public:
  explicit TranslateInfoBarBase(TranslateInfoBarDelegate* delegate);
  virtual ~TranslateInfoBarBase();

  // TranslateInfoBarView implementation:
  virtual void OriginalLanguageChanged() {}
  virtual void TargetLanguageChanged() {}

  // Overridden from views::View:
  virtual void Layout();
  virtual void PaintBackground(gfx::Canvas* canvas);

 protected:
  // Overridden from AnimationDelegate:
  virtual void AnimationProgressed(const Animation* animation);

  // Creates a label with the appropriate font and color for the translate
  // infobars.
  views::Label* CreateLabel(const string16& text);

  // Creates a menu-button with a custom appearance for the translate infobars.
  views::MenuButton* CreateMenuButton(const string16& text,
                                      bool normal_has_border,
                                      views::ViewMenuDelegate* menu_delegate);

  // Returns the location at which the menu triggered by |menu_button| should be
  // positioned.
  gfx::Point DetermineMenuPosition(views::MenuButton* menu_button);

  // Convenience to retrieve the TranslateInfoBarDelegate for this infobar.
  TranslateInfoBarDelegate* GetDelegate() const;

  // The translate icon.
  views::ImageView* icon_;

  InfoBarBackground normal_background_;
  InfoBarBackground error_background_;
  scoped_ptr<SlideAnimation> background_color_animation_;

 private:
  // Returns the background that should be displayed when not animating.
  const InfoBarBackground& GetBackground() const;

  // Paints |background| to |canvas| with the opacity level based on
  // |animation_value|.
  void FadeBackground(gfx::Canvas* canvas,
                      double animation_value,
                      const InfoBarBackground& background);

  DISALLOW_COPY_AND_ASSIGN(TranslateInfoBarBase);
};

#endif  // CHROME_BROWSER_VIEWS_INFOBARS_TRANSLATE_INFOBAR_BASE_H_
