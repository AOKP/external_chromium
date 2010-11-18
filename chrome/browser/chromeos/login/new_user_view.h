// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_NEW_USER_VIEW_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_NEW_USER_VIEW_H_
#pragma once

#include <string>

#include "chrome/browser/chromeos/login/language_switch_menu.h"
#include "views/accelerator.h"
#include "views/controls/button/button.h"
#include "views/controls/button/menu_button.h"
#include "views/controls/link.h"
#include "views/controls/textfield/textfield.h"
#include "views/view.h"

namespace views {
class Label;
class NativeButton;
class Throbber;
}  // namespace views

namespace chromeos {

// View that is used for new user login. It asks for username and password,
// allows to specify language preferences or initiate new account creation.
class NewUserView : public views::View,
                    public views::Textfield::Controller,
                    public views::LinkController,
                    public views::ButtonListener {
 public:
  // Delegate class to get notifications from the view.
  class Delegate {
  public:
    virtual ~Delegate() {}

    // User provided |username|, |password| and initiated login.
    virtual void OnLogin(const std::string& username,
                         const std::string& password) = 0;

    // Initiates off the record (incognito) login.
    virtual void OnLoginOffTheRecord() = 0;

    // User initiated new account creation.
    virtual void OnCreateAccount() = 0;

    // Adds start URL that will be opened after login.
    virtual void AddStartUrl(const GURL& start_url) = 0;

    // User started typing so clear all error messages.
    virtual void ClearErrors() = 0;

    // User tries to navigate away from NewUserView pod.
    virtual void NavigateAway() = 0;
  };

  // If |need_border| is true, RoundedRect border and background are required.
  NewUserView(Delegate* delegate,
              bool need_border,
              bool need_guest_link);

  virtual ~NewUserView();

  // Initialize view layout.
  void Init();

  // Update strings from the resources. Executed on language change.
  void UpdateLocalizedStrings();

  // Resets password text and sets the enabled state of the password.
  void ClearAndEnablePassword();

  // Resets password and username text and focuses on username.
  void ClearAndEnableFields();

  // Stops throbber shown during login.
  void StopThrobber();

  // Returns bounds of password field in screen coordinates.
  gfx::Rect GetPasswordBounds() const;

  // Returns bounds of username field in screen coordinates.
  gfx::Rect GetUsernameBounds() const;

  // Overridden from views::View:
  virtual gfx::Size GetPreferredSize();
  virtual void Layout();
  virtual void RequestFocus();

  // Setters for textfields.
  void SetUsername(const std::string& username);
  void SetPassword(const std::string& password);

  // Attempt to login with the current field values.
  void Login();

  // Overridden from views::Textfield::Controller
  // Not thread-safe, by virtue of using SetupSession().
  virtual bool HandleKeystroke(views::Textfield* sender,
                               const views::Textfield::Keystroke& keystroke);
  virtual void ContentsChanged(views::Textfield* sender,
                               const string16& new_contents);

  // Overridden from views::ButtonListener.
  virtual void ButtonPressed(views::Button* sender, const views::Event& event);

  // Overridden from views::LinkController.
  virtual void LinkActivated(views::Link* source, int event_flags);

  virtual bool AcceleratorPressed(const views::Accelerator& accelerator);

 protected:
  // views::View overrides:
  virtual void ViewHierarchyChanged(bool is_add,
                                    views::View *parent,
                                    views::View *child);
  virtual void NativeViewHierarchyChanged(bool attached,
                                          gfx::NativeView native_view,
                                          views::RootView* root_view);
  virtual void OnLocaleChanged();
  void AddChildView(View* view);

 private:
  // Enables/disables input controls (textfields, buttons).
  void EnableInputControls(bool enabled);
  void FocusFirstField();

  // Creates Link control and adds it as a child.
  void InitLink(views::Link** link);

  // Delete and recreate native controls that fail to update preferred size
  // after text/locale update.
  void RecreatePeculiarControls();

  // Enable or disable the |sign_in_button_| based on the contents of the
  // |username_field_| and |password_field_|. If there is text in both the
  // button is enabled, otherwise it's disabled.
  void UpdateSignInButtonState();

  // Create view with specified solid background and add it as  child.
  views::View* CreateSplitter(SkColor color);

  // Screen controls.
  // NOTE: sign_in_button_ and languages_menubutton_ are handled with
  // special care: they are recreated on any text/locale change
  // because they are not resized properly.
  views::Textfield* username_field_;
  views::Textfield* password_field_;
  views::Label* title_label_;
  views::Label* title_hint_label_;
  views::View* splitter_up1_;
  views::View* splitter_up2_;
  views::View* splitter_down1_;
  views::View* splitter_down2_;
  views::NativeButton* sign_in_button_;
  views::Link* create_account_link_;
  views::Link* guest_link_;
  views::MenuButton* languages_menubutton_;
  views::Throbber* throbber_;

  views::Accelerator accel_focus_pass_;
  views::Accelerator accel_focus_user_;
  views::Accelerator accel_login_off_the_record_;
  views::Accelerator accel_enable_accessibility_;

  // Notifications receiver.
  Delegate* delegate_;

  ScopedRunnableMethodFactory<NewUserView> focus_grabber_factory_;

  LanguageSwitchMenu language_switch_menu_;

  // Indicates that this view was created when focus manager was unavailable
  // (on the hidden tab, for example).
  bool focus_delayed_;

  // True when login is in process.
  bool login_in_process_;

  // If true, this view needs RoundedRect border and background.
  bool need_border_;

  // Whether Guest Mode link is needed.
  bool need_guest_link_;

  // Whether create account link is needed. Set to false for now but we may
  // need it back in near future.
  bool need_create_account_;

  // Ordinal position of controls inside view layout.
  int languages_menubutton_order_;
  int sign_in_button_order_;

  FRIEND_TEST_ALL_PREFIXES(LoginScreenTest, IncognitoLogin);
  friend class LoginScreenTest;

  DISALLOW_COPY_AND_ASSIGN(NewUserView);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_NEW_USER_VIEW_H_
