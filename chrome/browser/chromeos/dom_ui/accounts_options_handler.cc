// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/dom_ui/accounts_options_handler.h"

#include "app/l10n_util.h"
#include "base/json/json_reader.h"
#include "base/scoped_ptr.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/user_cros_settings_provider.h"
#include "chrome/browser/dom_ui/dom_ui_util.h"
#include "grit/generated_resources.h"

namespace chromeos {

AccountsOptionsHandler::AccountsOptionsHandler()
    : CrosOptionsPageUIHandler(new UserCrosSettingsProvider()) {
}

AccountsOptionsHandler::~AccountsOptionsHandler() {
}

void AccountsOptionsHandler::RegisterMessages() {
  DCHECK(dom_ui_);
  dom_ui_->RegisterMessageCallback("whitelistUser",
      NewCallback(this, &AccountsOptionsHandler::WhitelistUser));
  dom_ui_->RegisterMessageCallback("unwhitelistUser",
      NewCallback(this, &AccountsOptionsHandler::UnwhitelistUser));
  dom_ui_->RegisterMessageCallback("fetchUserPictures",
      NewCallback(this, &AccountsOptionsHandler::FetchUserPictures));
  dom_ui_->RegisterMessageCallback("whitelistExistingUsers",
      NewCallback(this, &AccountsOptionsHandler::WhitelistExistingUsers));
}

void AccountsOptionsHandler::GetLocalizedValues(
    DictionaryValue* localized_strings) {
  DCHECK(localized_strings);
  localized_strings->SetString("accountsPage", l10n_util::GetStringUTF16(
      IDS_OPTIONS_ACCOUNTS_TAB_LABEL));

  localized_strings->SetString("allow_BWSI", l10n_util::GetStringUTF16(
      IDS_OPTIONS_ACCOUNTS_ALLOW_BWSI_DESCRIPTION));
  localized_strings->SetString("allow_guest",l10n_util::GetStringUTF16(
      IDS_OPTIONS_ACCOUNTS_ALLOW_GUEST_DESCRIPTION));
  localized_strings->SetString("show_user_on_signin",l10n_util::GetStringUTF16(
      IDS_OPTIONS_ACCOUNTS_SHOW_USER_NAMES_ON_SINGIN_DESCRIPTION));
  localized_strings->SetString("username_edit_hint",l10n_util::GetStringUTF16(
      IDS_OPTIONS_ACCOUNTS_USERNAME_EDIT_HINT));
  localized_strings->SetString("username_format",l10n_util::GetStringUTF16(
      IDS_OPTIONS_ACCOUNTS_USERNAME_FORMAT));
  localized_strings->SetString("add_users",l10n_util::GetStringUTF16(
      IDS_OPTIONS_ACCOUNTS_ADD_USERS));

  localized_strings->SetString("current_user_is_owner",
      UserManager::Get()->current_user_is_owner() ?
      ASCIIToUTF16("true") : ASCIIToUTF16("false"));
}

UserCrosSettingsProvider* AccountsOptionsHandler::users_settings() const {
  return static_cast<UserCrosSettingsProvider*>(settings_provider_.get());
}

void AccountsOptionsHandler::WhitelistUser(const ListValue* args) {
  std::string email;
  if (!args->GetString(0, &email)) {
    return;
  }

  users_settings()->WhitelistUser(email);
}

void AccountsOptionsHandler::UnwhitelistUser(const ListValue* args) {
  std::string email;
  if (!args->GetString(0, &email)) {
    return;
  }

  users_settings()->UnwhitelistUser(email);
}

void AccountsOptionsHandler::FetchUserPictures(const ListValue* args) {
  DictionaryValue user_pictures;

  std::vector<UserManager::User> users = UserManager::Get()->GetUsers();
  for (std::vector<UserManager::User>::const_iterator it = users.begin();
       it < users.end(); ++it) {
    if (!it->image().isNull()) {
      StringValue* picture = new StringValue(
          dom_ui_util::GetImageDataUrl(it->image()));
      // SetWithoutPathExpansion because email has "." in it.
      user_pictures.SetWithoutPathExpansion(it->email(), picture);
    }
  }

  dom_ui_->CallJavascriptFunction(L"AccountsOptions.setUserPictures",
      user_pictures);
}

void AccountsOptionsHandler::WhitelistExistingUsers(const ListValue* args) {
  ListValue whitelist_users;

  std::vector<UserManager::User> users = UserManager::Get()->GetUsers();
  for (std::vector<UserManager::User>::const_iterator it = users.begin();
       it < users.end(); ++it) {
    const std::string& email = it->email();
    if (!UserCrosSettingsProvider::IsEmailInCachedWhitelist(email)) {
      DictionaryValue* user_dict = new DictionaryValue;
      user_dict->SetString("name", it->GetDisplayName());
      user_dict->SetString("email", email);
      user_dict->SetBoolean("owner", false);

      whitelist_users.Append(user_dict);
    }
  }

  dom_ui_->CallJavascriptFunction(L"AccountsOptions.addUsers", whitelist_users);
}

}  // namespace chromeos
