// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_CAPTCHA_VIEW_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_CAPTCHA_VIEW_H_
#pragma once

#include <string>

#include "chrome/browser/chromeos/login/image_decoder.h"
#include "googleurl/src/gurl.h"
#include "views/controls/button/button.h"
#include "views/controls/textfield/textfield.h"
#include "views/window/dialog_delegate.h"

namespace views {
class ImageView;
class TextButton;
class View;
class Window;
}  // namespace views

namespace chromeos {

// A dialog box that shows a CAPTCHA image and allows user to input response.
class CaptchaView : public views::View,
                    public views::DialogDelegate,
                    public views::Textfield::Controller,
                    public ImageDecoder::Delegate,
                    public views::ButtonListener {
 public:
  class Delegate {
   public:
    // Called when CAPTCHA answer has been entered.
    virtual void OnCaptchaEntered(const std::string& captcha) = 0;

   protected:
     virtual ~Delegate() {}
  };

  // |captcha_url| represents CAPTCHA image URL.
  // |is_standalone| is true when CaptchaView is not presented as dialog.
  CaptchaView(const GURL& captcha_url, bool is_standalone);
  virtual ~CaptchaView() {}

  // views::DialogDelegate overrides:
  virtual bool Accept();

  // views::WindowDelegate overrides:
  virtual bool IsModal() const { return true; }
  virtual views::View* GetContentsView() { return this; }

  // views::View overrides:
  virtual std::wstring GetWindowTitle() const;

  // views::Textfield::Controller implementation:
  virtual void ContentsChanged(views::Textfield* sender,
                                 const string16& new_contents) {}
  virtual bool HandleKeystroke(views::Textfield* sender,
                               const views::Textfield::Keystroke& keystroke);

  // Overriden from ImageDownloader::Delegate:
  virtual void OnImageDecoded(const SkBitmap& decoded_image);

  // Overridden from views::ButtonListener.
  virtual void ButtonPressed(views::Button* sender, const views::Event& event);

  // Initializes UI.
  void Init();

  void set_delegate(Delegate* delegate) {
    delegate_ = delegate;
  }

  // Instructs to download and display another captcha image.
  // Is used when same CaptchaView is reused.
  void SetCaptchaURL(const GURL& captcha_url);

 protected:
  // views::View overrides:
  virtual gfx::Size GetPreferredSize();
  virtual void ViewHierarchyChanged(bool is_add,
                                    views::View* parent,
                                    views::View* child);

 private:
  Delegate* delegate_;
  GURL captcha_url_;
  views::ImageView* captcha_image_;
  views::Textfield* captcha_textfield_;

  // True when view is not hosted inside dialog,
  // thus should draw OK button/background.
  bool is_standalone_;

  // Used in standalone mode.
  views::TextButton* ok_button_;

  DISALLOW_COPY_AND_ASSIGN(CaptchaView);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_CAPTCHA_VIEW_H_
