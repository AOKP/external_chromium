// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/dom_ui/font_settings_handler.h"

#include "app/l10n_util.h"
#include "base/basictypes.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/values.h"
#include "chrome/browser/character_encoding.h"
#include "chrome/browser/dom_ui/font_settings_utils.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profile.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/notification_type.h"
#include "chrome/common/pref_names.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"

FontSettingsHandler::FontSettingsHandler() {
}

FontSettingsHandler::~FontSettingsHandler() {
}

void FontSettingsHandler::GetLocalizedValues(
    DictionaryValue* localized_strings) {
  DCHECK(localized_strings);

  localized_strings->SetString("fontSettingsTitle",
      l10n_util::GetStringUTF16(IDS_FONT_LANGUAGE_SETTING_FONT_TAB_TITLE));
  localized_strings->SetString("fontSettingsFontTitle",
      l10n_util::GetStringUTF16(
          IDS_FONT_LANGUAGE_SETTING_FONT_SUB_DIALOG_FONT_TITLE));
  localized_strings->SetString("fontSettingsSerifLabel",
      l10n_util::GetStringUTF16(
          IDS_FONT_LANGUAGE_SETTING_FONT_SELECTOR_SERIF_LABEL));
  localized_strings->SetString("fontSettingsSansSerifLabel",
      l10n_util::GetStringUTF16(
          IDS_FONT_LANGUAGE_SETTING_FONT_SELECTOR_SANS_SERIF_LABEL));
  localized_strings->SetString("fontSettingsFixedWidthLabel",
      l10n_util::GetStringUTF16(
          IDS_FONT_LANGUAGE_SETTING_FONT_SELECTOR_FIXED_WIDTH_LABEL));
  localized_strings->SetString("fontSettingsSizeLabel",
      l10n_util::GetStringUTF16(
          IDS_FONT_LANGUAGE_SETTING_FONT_SIZE_SELECTOR_LABEL));
  localized_strings->SetString("fontSettingsEncodingTitle",
      l10n_util::GetStringUTF16(
          IDS_FONT_LANGUAGE_SETTING_FONT_SUB_DIALOG_ENCODING_TITLE));
  localized_strings->SetString("fontSettingsEncodingLabel",
      l10n_util::GetStringUTF16(
          IDS_FONT_LANGUAGE_SETTING_FONT_DEFAULT_ENCODING_SELECTOR_LABEL));

  // Fonts
  ListValue* font_list = FontSettingsUtilities::GetFontsList();
  if (font_list) localized_strings->Set("fontSettingsFontList", font_list);

  // Font sizes
  int font_sizes[] = { 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20, 22, 24, 26,
                       28, 30, 32, 34, 36, 40, 44, 48, 56, 64, 72 };
  int count = arraysize(font_sizes);
  ListValue* font_size_list = new ListValue;
  for (int i = 0; i < count; i++) {
    ListValue* option = new ListValue();
    option->Append(Value::CreateIntegerValue(font_sizes[i]));
    option->Append(Value::CreateStringValue(base::IntToString(font_sizes[i])));
    font_size_list->Append(option);
  }
  localized_strings->Set("fontSettingsFontSizeList", font_size_list);

  // Encodings
  count = CharacterEncoding::GetSupportCanonicalEncodingCount();
  ListValue* encoding_list = new ListValue;
  for (int i = 0; i < count; ++i) {
    int cmd_id = CharacterEncoding::GetEncodingCommandIdByIndex(i);
    std::string encoding =
        CharacterEncoding::GetCanonicalEncodingNameByCommandId(cmd_id);
    string16 name =
        CharacterEncoding::GetCanonicalEncodingDisplayNameByCommandId(cmd_id);

    ListValue* option = new ListValue();
    option->Append(Value::CreateStringValue(encoding));
    option->Append(Value::CreateStringValue(name));
    encoding_list->Append(option);
  }
  localized_strings->Set("fontSettingsEncodingList", encoding_list);
}

void FontSettingsHandler::Initialize() {
  SetupSerifFontPreview();
  SetupSansSerifFontPreview();
  SetupFixedFontPreview();
}

DOMMessageHandler* FontSettingsHandler::Attach(DOMUI* dom_ui) {
  // Call through to superclass.
  DOMMessageHandler* handler = OptionsPageUIHandler::Attach(dom_ui);

  // Perform validation for saved fonts.
  DCHECK(dom_ui_);
  PrefService* pref_service = dom_ui_->GetProfile()->GetPrefs();
  FontSettingsUtilities::ValidateSavedFonts(pref_service);

  // Register for preferences that we need to observe manually.
  serif_font_.Init(prefs::kWebKitSerifFontFamily, pref_service, this);
  sans_serif_font_.Init(prefs::kWebKitSansSerifFontFamily, pref_service, this);
  fixed_font_.Init(prefs::kWebKitFixedFontFamily, pref_service, this);
  default_font_size_.Init(prefs::kWebKitDefaultFontSize, pref_service, this);
  default_fixed_font_size_.Init(prefs::kWebKitDefaultFixedFontSize,
                                pref_service, this);

  // Return result from the superclass.
  return handler;
}

void FontSettingsHandler::Observe(NotificationType type,
                                  const NotificationSource& source,
                                  const NotificationDetails& details) {
  if (type == NotificationType::PREF_CHANGED) {
    std::string* pref_name = Details<std::string>(details).ptr();
    if (*pref_name == prefs::kWebKitSerifFontFamily ||
        *pref_name == prefs::kWebKitDefaultFontSize) {
      SetupSerifFontPreview();
    } else if (*pref_name == prefs::kWebKitSansSerifFontFamily ||
               *pref_name == prefs::kWebKitDefaultFontSize) {
      SetupSansSerifFontPreview();
    } else if (*pref_name == prefs::kWebKitFixedFontFamily ||
               *pref_name == prefs::kWebKitDefaultFixedFontSize) {
      SetupFixedFontPreview();
    }
  }
}

void FontSettingsHandler::SetupSerifFontPreview() {
  DCHECK(dom_ui_);
  StringValue font_value(serif_font_.GetValue());
  FundamentalValue size_value(default_font_size_.GetValue());
  dom_ui_->CallJavascriptFunction(
      L"FontSettings.setupSerifFontPreview", font_value, size_value);
}

void FontSettingsHandler::SetupSansSerifFontPreview() {
  DCHECK(dom_ui_);
  StringValue font_value(sans_serif_font_.GetValue());
  FundamentalValue size_value(default_font_size_.GetValue());
  dom_ui_->CallJavascriptFunction(
      L"FontSettings.setupSansSerifFontPreview", font_value, size_value);
}

void FontSettingsHandler::SetupFixedFontPreview() {
  DCHECK(dom_ui_);
  StringValue font_value(fixed_font_.GetValue());
  FundamentalValue size_value(default_fixed_font_size_.GetValue());
  dom_ui_->CallJavascriptFunction(
      L"FontSettings.setupFixedFontPreview", font_value, size_value);
}

