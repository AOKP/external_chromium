// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_OPTIONS_WIFI_CONFIG_VIEW_H_
#define CHROME_BROWSER_CHROMEOS_OPTIONS_WIFI_CONFIG_VIEW_H_
#pragma once

#include <string>

#include "base/gtest_prod_util.h"
#include "base/string16.h"
#include "chrome/browser/chromeos/cros/network_library.h"
#include "chrome/browser/shell_dialogs.h"
#include "views/controls/button/button.h"
#include "views/controls/button/checkbox.h"
#include "views/controls/button/image_button.h"
#include "views/controls/button/native_button.h"
#include "views/controls/textfield/textfield.h"
#include "views/view.h"

class FilePath;

namespace chromeos {

class NetworkConfigView;

// A dialog box for showing a password textfield.
class WifiConfigView : public views::View,
                       public views::Textfield::Controller,
                       public views::ButtonListener,
                       public SelectFileDialog::Listener {
 public:
  WifiConfigView(NetworkConfigView* parent, WifiNetwork wifi);
  explicit WifiConfigView(NetworkConfigView* parent);
  virtual ~WifiConfigView() {}

  // views::Textfield::Controller methods.
  virtual void ContentsChanged(views::Textfield* sender,
                               const string16& new_contents);
  virtual bool HandleKeystroke(views::Textfield* sender,
                               const views::Textfield::Keystroke& keystroke);

  // views::ButtonListener
  virtual void ButtonPressed(views::Button* sender, const views::Event& event);

  // SelectFileDialog::Listener implementation.
  virtual void FileSelected(const FilePath& path, int index, void* params);

  // Login to network.
  virtual bool Login();

  // Save network information.
  virtual bool Save();

  // Get the typed in ssid.
  const std::string GetSSID() const;
  // Get the typed in passphrase.
  const std::string GetPassphrase() const;

  // Returns true if the textfields are non-empty and we can login.
  bool can_login() const { return can_login_; }

  // Focus the first field in the UI.
  void FocusFirstField();

 private:
  FRIEND_TEST_ALL_PREFIXES(WifiConfigViewTest, NoChangeSaveTest);
  FRIEND_TEST_ALL_PREFIXES(WifiConfigViewTest, ChangeAutoConnectSaveTest);
  FRIEND_TEST_ALL_PREFIXES(WifiConfigViewTest, ChangePasswordSaveTest);

  // Initializes UI.
  void Init();

  // Updates state of the Login button.
  void UpdateCanLogin();

  // Updates state of the "view password" button.
  void UpdateCanViewPassword();

  NetworkConfigView* parent_;

  bool other_network_;

  // Whether or not we can log in. This gets recalculated when textfield
  // contents change.
  bool can_login_;

  WifiNetwork wifi_;

  views::Textfield* ssid_textfield_;
  views::Textfield* identity_textfield_;
  views::NativeButton* certificate_browse_button_;
  scoped_refptr<SelectFileDialog> select_file_dialog_;
  std::string certificate_path_;
  views::Textfield* passphrase_textfield_;
  views::ImageButton* passphrase_visible_button_;
  views::Checkbox* autoconnect_checkbox_;

  DISALLOW_COPY_AND_ASSIGN(WifiConfigView);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_OPTIONS_WIFI_CONFIG_VIEW_H_
