// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_OPTIONS_WIFI_CONFIG_VIEW_H_
#define CHROME_BROWSER_CHROMEOS_OPTIONS_WIFI_CONFIG_VIEW_H_
#pragma once

#include <string>

#include "app/combobox_model.h"
#include "base/gtest_prod_util.h"
#include "base/string16.h"
#include "chrome/browser/chromeos/cros/network_library.h"
#include "chrome/browser/shell_dialogs.h"
#include "views/controls/button/button.h"
#include "views/controls/button/checkbox.h"
#include "views/controls/button/image_button.h"
#include "views/controls/button/native_button.h"
#include "views/controls/combobox/combobox.h"
#include "views/controls/textfield/textfield.h"
#include "views/view.h"

class FilePath;

namespace chromeos {

class NetworkConfigView;

// A dialog box for showing a password textfield.
class WifiConfigView : public views::View,
                       public views::Textfield::Controller,
                       public views::ButtonListener,
                       public views::Combobox::Listener,
                       public SelectFileDialog::Listener {
 public:
  WifiConfigView(NetworkConfigView* parent, const WifiNetwork* wifi);
  explicit WifiConfigView(NetworkConfigView* parent);
  virtual ~WifiConfigView();

  // views::Textfield::Controller methods.
  virtual void ContentsChanged(views::Textfield* sender,
                               const string16& new_contents);
  virtual bool HandleKeyEvent(views::Textfield* sender,
                              const views::KeyEvent& key_event);

  // views::ButtonListener
  virtual void ButtonPressed(views::Button* sender, const views::Event& event);

  // views::Combobox::Listener
  virtual void ItemChanged(views::Combobox* combo_box,
                           int prev_index, int new_index);

  // SelectFileDialog::Listener implementation.
  virtual void FileSelected(const FilePath& path, int index, void* params);

  // Login to network. Returns false if the dialog should remain open.
  virtual bool Login();

  // Save network information.
  virtual bool Save();

  // Cancel the dialog.
  virtual void Cancel();

  // Get the typed in ssid.
  const std::string GetSSID() const;
  // Get the typed in passphrase.
  const std::string GetPassphrase() const;

  // Returns true if the textfields are non-empty and we can login.
  bool can_login() const { return can_login_; }

 private:
  class SecurityComboboxModel : public ComboboxModel {
   public:
    SecurityComboboxModel() {}
    virtual ~SecurityComboboxModel() {}
    virtual int GetItemCount();
    virtual string16 GetItemAt(int index);
   private:
    DISALLOW_COPY_AND_ASSIGN(SecurityComboboxModel);
  };

  // Initializes UI.
  void Init();

  // Updates state of the Login button.
  void UpdateCanLogin();

  // Updates the error text label.
  void UpdateErrorLabel(bool failed);

  NetworkConfigView* parent_;

  // Whether or not we can log in. This gets recalculated when textfield
  // contents change.
  bool can_login_;

  scoped_ptr<WifiNetwork> wifi_;

  views::Textfield* ssid_textfield_;
  views::Textfield* identity_textfield_;
  views::NativeButton* certificate_browse_button_;
  scoped_refptr<SelectFileDialog> select_file_dialog_;
  std::string certificate_path_;
  views::Combobox* security_combobox_;
  views::Textfield* passphrase_textfield_;
  views::ImageButton* passphrase_visible_button_;
  views::Label* error_label_;

  DISALLOW_COPY_AND_ASSIGN(WifiConfigView);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_OPTIONS_WIFI_CONFIG_VIEW_H_
