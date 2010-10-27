// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/dom_ui/language_pinyin_options_handler.h"

#include "app/l10n_util.h"
#include "base/values.h"
#include "chrome/browser/chromeos/dom_ui/language_options_util.h"
#include "chrome/browser/chromeos/language_preferences.h"
#include "grit/generated_resources.h"

namespace {
const char kI18nPrefix[] = "Pinyin";
}  // namespace

namespace chromeos {

LanguagePinyinOptionsHandler::LanguagePinyinOptionsHandler() {
}

LanguagePinyinOptionsHandler::~LanguagePinyinOptionsHandler() {
}

void LanguagePinyinOptionsHandler::GetLocalizedValues(
    DictionaryValue* localized_strings) {
  DCHECK(localized_strings);
  // Language Pinyin page - ChromeOS
  for (size_t i = 0; i < language_prefs::kNumPinyinBooleanPrefs; ++i) {
    localized_strings->SetString(
        GetI18nContentValue(language_prefs::kPinyinBooleanPrefs[i],
                            kI18nPrefix),
        l10n_util::GetStringUTF16(
            language_prefs::kPinyinBooleanPrefs[i].message_id));
  }

  localized_strings->SetString(
      GetI18nContentValue(language_prefs::kPinyinDoublePinyinSchema,
                          kI18nPrefix),
      l10n_util::GetStringUTF16(
          language_prefs::kPinyinDoublePinyinSchema.label_message_id));
  ListValue* list_value = new ListValue();
  for (size_t i = 0;
       i < language_prefs::LanguageMultipleChoicePreference<int>::kMaxItems;
       ++i) {
    if (language_prefs::kPinyinDoublePinyinSchema.values_and_ids[i].
        item_message_id == 0)
      break;
    ListValue* option = new ListValue();
    option->Append(Value::CreateIntegerValue(
        language_prefs::kPinyinDoublePinyinSchema.values_and_ids[i].
        ibus_config_value));
    option->Append(Value::CreateStringValue(l10n_util::GetStringUTF16(
        language_prefs::kPinyinDoublePinyinSchema.values_and_ids[i].
        item_message_id)));
    list_value->Append(option);
  }
  localized_strings->Set(
      GetTemplateDataPropertyName(language_prefs::kPinyinDoublePinyinSchema,
                                  kI18nPrefix),
      list_value);
}

}  // namespace chromeos
