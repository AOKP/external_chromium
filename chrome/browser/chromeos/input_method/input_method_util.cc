// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/input_method/input_method_util.h"

#include <algorithm>
#include <functional>
#include <map>
#include <utility>

#include "unicode/uloc.h"

#include "app/l10n_util.h"
#include "app/l10n_util_collator.h"
#include "base/basictypes.h"
#include "base/hash_tables.h"
#include "base/scoped_ptr.h"
#include "base/singleton.h"
#include "base/string_split.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/cros/keyboard_library.h"
#include "chrome/browser/chromeos/language_preferences.h"
#include "grit/generated_resources.h"

namespace {

// Mapping from input method ID to keyboard overlay ID, which specifies the
// layout and the glyphs of the keyboard overlay.
// TODO(mazda): Move this list to whitelist.txt (http://crosbug.com/9682)
const struct InputMethodIdToKeyboardOverlayId {
  const char* input_method_id;
  const char* keyboard_overlay_id;
} kInputMethodIdToKeyboardOverlayId[] = {
  { "xkb:nl::nld", "nl" },
  { "xkb:be::nld", "nl" },
  { "xkb:fr::fra", "fr" },
  { "xkb:be::fra", "fr" },
  { "xkb:ca::fra", "fr_CA" },
  { "xkb:ch:fr:fra", "fr" },
  { "xkb:de::ger", "de" },
  { "xkb:be::ger", "de" },
  { "xkb:ch::ger", "de" },
  { "mozc", "en_US" },
  { "mozc-jp", "ja" },
  { "mozc-dv", "en_US_dvorak" },
  { "xkb:jp::jpn", "ja" },
  { "xkb:ru::rus", "ru" },
  { "xkb:ru:phonetic:rus", "ru" },
  { "m17n:th:kesmanee", "th" },
  { "m17n:th:pattachote", "th" },
  { "m17n:th:tis820", "th" },
  { "chewing", "zh_TW" },
  { "m17n:zh:cangjie", "zh_TW" },
  { "m17n:zh:quick", "zh_TW" },
  { "m17n:vi:tcvn", "vi" },
  { "m17n:vi:telex", "vi" },
  { "m17n:vi:viqr", "vi" },
  { "m17n:vi:vni", "vi" },
  { "xkb:us::eng", "en_US" },
  { "xkb:us:intl:eng", "en_US" },
  { "xkb:us:altgr-intl:eng", "en_US" },
  { "xkb:us:dvorak:eng", "en_US_dvorak" },
  // TODO(mazda): Add keyboard overlay definition for US Colemak.
  { "xkb:us:colemak:eng", "en_US" },
  { "hangul", "ko" },
  { "pinyin", "zh_CN" },
  { "m17n:ar:kbd", "ar" },
  { "m17n:hi:itrans", "hi" },
  { "m17n:fa:isiri", "ar" },
  { "xkb:br::por", "pt_BR" },
  { "xkb:bg::bul", "bg" },
  { "xkb:bg:phonetic:bul", "bg" },
  { "xkb:ca:eng:eng", "ca" },
  { "xkb:cz::cze", "cs" },
  { "xkb:ee::est", "et" },
  { "xkb:es::spa", "es" },
  { "xkb:es:cat:cat", "ca" },
  { "xkb:dk::dan", "da" },
  { "xkb:gr::gre", "el" },
  { "xkb:il::heb", "iw" },
  { "xkb:kr:kr104:kor", "ko" },
  { "xkb:latam::spa", "es_419" },
  { "xkb:lt::lit", "lt" },
  { "xkb:lv:apostrophe:lav", "lv" },
  { "xkb:hr::scr", "hr" },
  { "xkb:gb:extd:eng", "en_GB" },
  { "xkb:fi::fin", "fi" },
  { "xkb:hu::hun", "hu" },
  { "xkb:it::ita", "it" },
  { "xkb:no::nob", "no" },
  { "xkb:pl::pol", "pl" },
  { "xkb:pt::por", "pt_PT" },
  { "xkb:ro::rum", "ro" },
  { "xkb:se::swe", "sv" },
  { "xkb:sk::slo", "sk" },
  { "xkb:si::slv", "sl" },
  { "xkb:rs::srp", "sr" },
  { "xkb:tr::tur", "tr" },
  { "xkb:ua::ukr", "uk" },
};

// Map from language code to associated input method IDs, etc.
typedef std::multimap<std::string, std::string> LanguageCodeToIdsMap;
struct IdMaps {
  scoped_ptr<LanguageCodeToIdsMap> language_code_to_ids;
  scoped_ptr<std::map<std::string, std::string> > id_to_language_code;
  scoped_ptr<std::map<std::string, std::string> > id_to_display_name;
  scoped_ptr<std::map<std::string, std::string> > id_to_keyboard_overlay_id;

  // Returns the singleton instance.
  static IdMaps* GetInstance() {
    return Singleton<IdMaps>::get();
  }

  void ReloadMaps() {
    chromeos::InputMethodLibrary* library =
        chromeos::CrosLibrary::Get()->GetInputMethodLibrary();
    scoped_ptr<chromeos::InputMethodDescriptors> supported_input_methods(
        library->GetSupportedInputMethods());
    if (supported_input_methods->size() <= 1) {
      LOG(ERROR) << "GetSupportedInputMethods returned a fallback ID";
      // TODO(yusukes): Handle this error in nicer way.
    }

    language_code_to_ids->clear();
    id_to_language_code->clear();
    id_to_display_name->clear();
    id_to_keyboard_overlay_id->clear();

    // Build the id to descriptor map for handling kExtraLanguages later.
    typedef std::map<std::string,
        const chromeos::InputMethodDescriptor*> DescMap;
    DescMap id_to_descriptor_map;

    for (size_t i = 0; i < supported_input_methods->size(); ++i) {
      const chromeos::InputMethodDescriptor& input_method =
          supported_input_methods->at(i);
      const std::string language_code =
          chromeos::input_method::GetLanguageCodeFromDescriptor(input_method);
      AddInputMethodToMaps(language_code, input_method);
      // Remember the pair.
      id_to_descriptor_map.insert(
          std::make_pair(input_method.id, &input_method));
    }

    for (size_t i = 0; i < arraysize(kInputMethodIdToKeyboardOverlayId); ++i) {
      InputMethodIdToKeyboardOverlayId id_pair =
          kInputMethodIdToKeyboardOverlayId[i];
      id_to_keyboard_overlay_id->insert(
          std::make_pair(id_pair.input_method_id, id_pair.keyboard_overlay_id));
    }

    // Go through the languages listed in kExtraLanguages.
    using chromeos::input_method::kExtraLanguages;
    for (size_t i = 0; i < arraysize(kExtraLanguages); ++i) {
      const char* language_code = kExtraLanguages[i].language_code;
      const char* input_method_id = kExtraLanguages[i].input_method_id;
      DescMap::const_iterator iter = id_to_descriptor_map.find(input_method_id);
      // If the associated input method descriptor is found, add the
      // language code and the input method.
      if (iter != id_to_descriptor_map.end()) {
        const chromeos::InputMethodDescriptor& input_method = *(iter->second);
        AddInputMethodToMaps(language_code, input_method);
      }
    }
  }

 private:
  IdMaps() : language_code_to_ids(new LanguageCodeToIdsMap),
             id_to_language_code(new std::map<std::string, std::string>),
             id_to_display_name(new std::map<std::string, std::string>),
             id_to_keyboard_overlay_id(new std::map<std::string, std::string>) {
    ReloadMaps();
  }

  void AddInputMethodToMaps(
      const std::string& language_code,
      const chromeos::InputMethodDescriptor& input_method) {
    language_code_to_ids->insert(
        std::make_pair(language_code, input_method.id));
    id_to_language_code->insert(
        std::make_pair(input_method.id, language_code));
    id_to_display_name->insert(std::make_pair(
        input_method.id,
        chromeos::input_method::GetStringUTF8(input_method.display_name)));
  }

  friend struct DefaultSingletonTraits<IdMaps>;

  DISALLOW_COPY_AND_ASSIGN(IdMaps);
};

const struct EnglishToResouceId {
  const char* english_string_from_ibus;
  int resource_id;
} kEnglishToResourceIdArray[] = {
  // For ibus-mozc: third_party/ibus-mozc/files/src/unix/ibus/.
  { "Direct input", IDS_STATUSBAR_IME_JAPANESE_IME_STATUS_DIRECT_INPUT },
  { "Hiragana", IDS_STATUSBAR_IME_JAPANESE_IME_STATUS_HIRAGANA },
  { "Katakana", IDS_STATUSBAR_IME_JAPANESE_IME_STATUS_KATAKANA },
  { "Half width katakana",  // small k is not a typo.
    IDS_STATUSBAR_IME_JAPANESE_IME_STATUS_HALF_WIDTH_KATAKANA },
  { "Latin", IDS_STATUSBAR_IME_JAPANESE_IME_STATUS_LATIN },
  { "Wide Latin", IDS_STATUSBAR_IME_JAPANESE_IME_STATUS_WIDE_LATIN },

  // For ibus-hangul: third_party/ibus-hangul/files/po/.
  { "Enable/Disable Hanja mode", IDS_STATUSBAR_IME_KOREAN_HANJA_MODE },

  // For ibus-pinyin: third_party/ibus-pinyin/files/po/.
  { "Chinese", IDS_STATUSBAR_IME_CHINESE_PINYIN_TOGGLE_CHINESE_ENGLISH },
  { "Full/Half width",
    IDS_STATUSBAR_IME_CHINESE_PINYIN_TOGGLE_FULL_HALF },
  { "Full/Half width punctuation",
    IDS_STATUSBAR_IME_CHINESE_PINYIN_TOGGLE_FULL_HALF_PUNCTUATION },
  { "Simplfied/Traditional Chinese",
    IDS_STATUSBAR_IME_CHINESE_PINYIN_TOGGLE_S_T_CHINESE },

  // For ibus-chewing: third_party/ibus-chewing/files/src/IBusChewingEngine.gob.
  { "Chi", IDS_STATUSBAR_IME_CHINESE_CHEWING_SWITCH_CHINESE_TO_ENGLISH },
  { "Eng", IDS_STATUSBAR_IME_CHINESE_CHEWING_SWITCH_ENGLISH_TO_CHINESE },
  { "Full", IDS_STATUSBAR_IME_CHINESE_CHEWING_SWITCH_FULL_TO_HALF },
  { "Half", IDS_STATUSBAR_IME_CHINESE_CHEWING_SWITCH_HALF_TO_FULL },

  // For the "Languages and Input" dialog.
  { "kbd (m17n)", IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_STANDARD_INPUT_METHOD },
  { "itrans (m17n)",  // also uses the "STANDARD_INPUT_METHOD" id.
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_STANDARD_INPUT_METHOD },
  { "cangjie (m17n)",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_CHINESE_CANGJIE_INPUT_METHOD },
  { "quick (m17n)",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_CHINESE_QUICK_INPUT_METHOD },
  { "isiri (m17n)",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_PERSIAN_ISIRI_2901_INPUT_METHOD },
  { "kesmanee (m17n)",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_THAI_KESMANEE_INPUT_METHOD },
  { "tis820 (m17n)",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_THAI_TIS820_INPUT_METHOD },
  { "pattachote (m17n)",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_THAI_PATTACHOTE_INPUT_METHOD },
  { "tcvn (m17n)",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_VIETNAMESE_TCVN_INPUT_METHOD },
  { "telex (m17n)",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_VIETNAMESE_TELEX_INPUT_METHOD },
  { "viqr (m17n)",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_VIETNAMESE_VIQR_INPUT_METHOD },
  { "vni (m17n)",
    IDS_OPTIONS_SETTINGS_LANGUAGES_M17N_VIETNAMESE_VNI_INPUT_METHOD },
  { "Bopomofo", IDS_OPTIONS_SETTINGS_LANGUAGES_BOPOMOFO_INPUT_METHOD },
  { "Chewing", IDS_OPTIONS_SETTINGS_LANGUAGES_CHEWING_INPUT_METHOD },
  { "Pinyin", IDS_OPTIONS_SETTINGS_LANGUAGES_PINYIN_INPUT_METHOD },
  { "Mozc (US keyboard layout)",
    IDS_OPTIONS_SETTINGS_LANGUAGES_JAPANESE_MOZC_US_INPUT_METHOD },
  { "Mozc (US Dvorak keyboard layout)",
    IDS_OPTIONS_SETTINGS_LANGUAGES_JAPANESE_MOZC_US_DV_INPUT_METHOD },
  { "Mozc (Japanese keyboard layout)",
    IDS_OPTIONS_SETTINGS_LANGUAGES_JAPANESE_MOZC_JP_INPUT_METHOD },
  { "Google Japanese Input (US keyboard layout)",
    IDS_OPTIONS_SETTINGS_LANGUAGES_JAPANESE_GOOGLE_US_INPUT_METHOD },
  { "Google Japanese Input (US Dvorak keyboard layout)",
    IDS_OPTIONS_SETTINGS_LANGUAGES_JAPANESE_GOOGLE_US_DV_INPUT_METHOD },
  { "Google Japanese Input (Japanese keyboard layout)",
    IDS_OPTIONS_SETTINGS_LANGUAGES_JAPANESE_GOOGLE_JP_INPUT_METHOD },
  { "Korean", IDS_OPTIONS_SETTINGS_LANGUAGES_KOREAN_INPUT_METHOD },

  // For ibus-xkb-layouts engine: third_party/ibus-xkb-layouts/files
  { "Japan", IDS_STATUSBAR_LAYOUT_JAPAN },
  { "Slovenia", IDS_STATUSBAR_LAYOUT_SLOVENIA },
  { "Germany", IDS_STATUSBAR_LAYOUT_GERMANY },
  { "Italy", IDS_STATUSBAR_LAYOUT_ITALY },
  { "Estonia", IDS_STATUSBAR_LAYOUT_ESTONIA },
  { "Hungary", IDS_STATUSBAR_LAYOUT_HUNGARY },
  { "Poland", IDS_STATUSBAR_LAYOUT_POLAND },
  { "Denmark", IDS_STATUSBAR_LAYOUT_DENMARK },
  { "Croatia", IDS_STATUSBAR_LAYOUT_CROATIA },
  { "Brazil", IDS_STATUSBAR_LAYOUT_BRAZIL },
  { "Serbia", IDS_STATUSBAR_LAYOUT_SERBIA },
  { "Czechia", IDS_STATUSBAR_LAYOUT_CZECHIA },
  { "USA - Dvorak", IDS_STATUSBAR_LAYOUT_USA_DVORAK },
  { "USA - Colemak", IDS_STATUSBAR_LAYOUT_USA_COLEMAK },
  { "Romania", IDS_STATUSBAR_LAYOUT_ROMANIA },
  { "USA", IDS_STATUSBAR_LAYOUT_USA },
  { "USA - International (AltGr dead keys)",
    IDS_STATUSBAR_LAYOUT_USA_EXTENDED },
  { "USA - International (with dead keys)",
    IDS_STATUSBAR_LAYOUT_USA_INTERNATIONAL },
  { "Lithuania", IDS_STATUSBAR_LAYOUT_LITHUANIA },
  { "United Kingdom - Extended - Winkeys",
    IDS_STATUSBAR_LAYOUT_UNITED_KINGDOM },
  { "Slovakia", IDS_STATUSBAR_LAYOUT_SLOVAKIA },
  { "Russia", IDS_STATUSBAR_LAYOUT_RUSSIA },
  { "Russia - Phonetic", IDS_STATUSBAR_LAYOUT_RUSSIA_PHONETIC },
  { "Greece", IDS_STATUSBAR_LAYOUT_GREECE },
  { "Belgium", IDS_STATUSBAR_LAYOUT_BELGIUM },
  { "Bulgaria", IDS_STATUSBAR_LAYOUT_BULGARIA },
  { "Bulgaria - Traditional phonetic", IDS_STATUSBAR_LAYOUT_BULGARIA_PHONETIC },
  { "Switzerland", IDS_STATUSBAR_LAYOUT_SWITZERLAND },
  { "Switzerland - French", IDS_STATUSBAR_LAYOUT_SWITZERLAND_FRENCH },
  { "Turkey", IDS_STATUSBAR_LAYOUT_TURKEY },
  { "Portugal", IDS_STATUSBAR_LAYOUT_PORTUGAL },
  { "Spain", IDS_STATUSBAR_LAYOUT_SPAIN },
  { "Finland", IDS_STATUSBAR_LAYOUT_FINLAND },
  { "Ukraine", IDS_STATUSBAR_LAYOUT_UKRAINE },
  { "Spain - Catalan variant with middle-dot L",
    IDS_STATUSBAR_LAYOUT_SPAIN_CATALAN },
  { "France", IDS_STATUSBAR_LAYOUT_FRANCE },
  { "Norway", IDS_STATUSBAR_LAYOUT_NORWAY },
  { "Sweden", IDS_STATUSBAR_LAYOUT_SWEDEN },
  { "Netherlands", IDS_STATUSBAR_LAYOUT_NETHERLANDS },
  { "Latin American", IDS_STATUSBAR_LAYOUT_LATIN_AMERICAN },
  { "Latvia - Apostrophe (') variant", IDS_STATUSBAR_LAYOUT_LATVIA },
  { "Canada", IDS_STATUSBAR_LAYOUT_CANADA },
  { "Canada - English", IDS_STATUSBAR_LAYOUT_CANADA_ENGLISH },
  { "Israel", IDS_STATUSBAR_LAYOUT_ISRAEL },
  { "Korea, Republic of - 101/104 key Compatible",
    IDS_STATUSBAR_LAYOUT_KOREA_104 },
};
const size_t kNumEntries = arraysize(kEnglishToResourceIdArray);

// There are some differences between ISO 639-2 (T) and ISO 639-2 B, and
// some language codes are not recognized by ICU (i.e. ICU cannot convert
// these codes to two-letter language codes and display names). Hence we
// convert these codes to ones that ICU recognize.
//
// See http://en.wikipedia.org/wiki/List_of_ISO_639-1_codes for details.
const char* kIso639VariantMapping[][2] = {
  { "cze", "ces" },
  { "ger", "deu" },
  { "gre", "ell" },
  // "scr" is not a ISO 639 code. For some reason, evdev.xml uses "scr" as
  // the language code for Croatian.
  { "scr", "hrv" },
  { "rum", "ron" },
  { "slo", "slk" },
};

// The comparator is used for sorting language codes by their
// corresponding language names, using the ICU collator.
struct CompareLanguageCodesByLanguageName
    : std::binary_function<const std::string&, const std::string&, bool> {
  explicit CompareLanguageCodesByLanguageName(icu::Collator* collator)
      : collator_(collator) {
  }

  // Calling GetLanguageDisplayNameFromCode() in the comparator is not
  // efficient, but acceptable as the function is cheap, and the language
  // list is short (about 40 at most).
  bool operator()(const std::string& s1, const std::string& s2) const {
    const std::wstring key1 =
        chromeos::input_method::GetLanguageDisplayNameFromCode(s1);
    const std::wstring key2 =
        chromeos::input_method::GetLanguageDisplayNameFromCode(s2);
    return l10n_util::StringComparator<std::wstring>(collator_)(key1, key2);
  }

 private:
  icu::Collator* collator_;
};

// The comparator is used for sorting input method ids by their
// corresponding language names, using the ICU collator.
struct CompareInputMethodIdsByLanguageName
    : std::binary_function<const std::string&, const std::string&, bool> {
  CompareInputMethodIdsByLanguageName(
      icu::Collator* collator,
      const std::map<std::string, std::string>& id_to_language_code_map)
      : comparator_(collator),
        id_to_language_code_map_(id_to_language_code_map) {
  }

  bool operator()(const std::string& s1, const std::string& s2) const {
    std::string language_code_1;
    std::map<std::string, std::string>::const_iterator iter =
        id_to_language_code_map_.find(s1);
    if (iter != id_to_language_code_map_.end()) {
      language_code_1 = iter->second;
    }
    std::string language_code_2;
    iter = id_to_language_code_map_.find(s2);
    if (iter != id_to_language_code_map_.end()) {
      language_code_2 = iter->second;
    }
    return comparator_(language_code_1, language_code_2);
  }

 private:
  const CompareLanguageCodesByLanguageName comparator_;
  const std::map<std::string, std::string>& id_to_language_code_map_;
};

bool GetLocalizedString(
    const std::string& english_string, string16 *out_string) {
  DCHECK(out_string);
  typedef base::hash_map<std::string, int> HashType;
  static HashType* english_to_resource_id = NULL;

  // Initialize the map if needed.
  if (!english_to_resource_id) {
    // We don't free this map.
    english_to_resource_id = new HashType(kNumEntries);
    for (size_t i = 0; i < kNumEntries; ++i) {
      const bool result = english_to_resource_id->insert(
          std::make_pair(kEnglishToResourceIdArray[i].english_string_from_ibus,
                         kEnglishToResourceIdArray[i].resource_id)).second;
      DCHECK(result) << "Duplicated string is found: "
                     << kEnglishToResourceIdArray[i].english_string_from_ibus;
    }
  }

  HashType::const_iterator iter = english_to_resource_id->find(english_string);
  if (iter == english_to_resource_id->end()) {
    // TODO(yusukes): Write Autotest which checks if all display names and all
    // property names for supported input methods are listed in the resource ID
    // array (crosbug.com/4572).
    LOG(ERROR) << "Resource ID is not found for: " << english_string;
    return false;
  }

  *out_string = l10n_util::GetStringUTF16(iter->second);
  return true;
};

}  // namespace

namespace chromeos {
namespace input_method {

std::wstring GetString(const std::string& english_string) {
  string16 localized_string;
  if (GetLocalizedString(english_string, &localized_string)) {
    return UTF16ToWide(localized_string);
  }
  return UTF8ToWide(english_string);
}

std::string GetStringUTF8(const std::string& english_string) {
  string16 localized_string;
  if (GetLocalizedString(english_string, &localized_string)) {
    return UTF16ToUTF8(localized_string);
  }
  return english_string;
}

string16 GetStringUTF16(const std::string& english_string) {
  string16 localized_string;
  if (GetLocalizedString(english_string, &localized_string)) {
    return localized_string;
  }
  return UTF8ToUTF16(english_string);
}

bool StringIsSupported(const std::string& english_string) {
  string16 localized_string;
  return GetLocalizedString(english_string, &localized_string);
}

std::string NormalizeLanguageCode(
    const std::string& language_code) {
  // Some ibus engines return locale codes like "zh_CN" as language codes.
  // Normalize these to like "zh-CN".
  if (language_code.size() >= 5 && language_code[2] == '_') {
    std::string copied_language_code = language_code;
    copied_language_code[2] = '-';
    // Downcase the language code part.
    for (size_t i = 0; i < 2; ++i) {
      copied_language_code[i] = base::ToLowerASCII(copied_language_code[i]);
    }
    // Upcase the country code part.
    for (size_t i = 3; i < copied_language_code.size(); ++i) {
      copied_language_code[i] = base::ToUpperASCII(copied_language_code[i]);
    }
    return copied_language_code;
  }
  // We only handle three-letter codes from here.
  if (language_code.size() != 3) {
    return language_code;
  }

  // Convert special language codes. See comments at kIso639VariantMapping.
  std::string copied_language_code = language_code;
  for (size_t i = 0; i < arraysize(kIso639VariantMapping); ++i) {
    if (language_code == kIso639VariantMapping[i][0]) {
      copied_language_code = kIso639VariantMapping[i][1];
    }
  }
  // Convert the three-letter code to two letter-code.
  UErrorCode error = U_ZERO_ERROR;
  char two_letter_code[ULOC_LANG_CAPACITY];
  uloc_getLanguage(copied_language_code.c_str(),
                   two_letter_code, sizeof(two_letter_code), &error);
  if (U_FAILURE(error)) {
    return language_code;
  }
  return two_letter_code;
}

bool IsKeyboardLayout(const std::string& input_method_id) {
  const bool kCaseInsensitive = false;
  return StartsWithASCII(input_method_id, "xkb:", kCaseInsensitive);
}

std::string GetLanguageCodeFromDescriptor(
    const InputMethodDescriptor& descriptor) {
  // Handle some Chinese input methods as zh-CN/zh-TW, rather than zh.
  // TODO: we should fix this issue in engines rather than here.
  if (descriptor.language_code == "zh") {
    if (descriptor.id == "pinyin") {
      return "zh-CN";
    } else if (descriptor.id == "bopomofo" ||
               descriptor.id == "chewing" ||
               descriptor.id == "m17n:zh:cangjie" ||
               descriptor.id == "m17n:zh:quick") {
      return "zh-TW";
    }
  }

  std::string language_code = NormalizeLanguageCode(descriptor.language_code);

  // Add country codes to language codes of some XKB input methods to make
  // these compatible with Chrome's application locale codes like "en-US".
  // TODO(satorux): Maybe we need to handle "es" for "es-419".
  // TODO: We should not rely on the format of the engine name. Should we add
  //       |country_code| in InputMethodDescriptor?
  if (IsKeyboardLayout(descriptor.id) &&
      (language_code == "en" ||
       language_code == "zh" ||
       language_code == "pt")) {
    std::vector<std::string> portions;
    base::SplitString(descriptor.id, ':', &portions);
    if (portions.size() >= 2 && !portions[1].empty()) {
      language_code.append("-");
      language_code.append(StringToUpperASCII(portions[1]));
    }
  }
  return language_code;
}

std::string GetLanguageCodeFromInputMethodId(
    const std::string& input_method_id) {
  // The code should be compatible with one of codes used for UI languages,
  // defined in app/l10_util.cc.
  const char kDefaultLanguageCode[] = "en-US";
  std::map<std::string, std::string>::const_iterator iter
      = IdMaps::GetInstance()->id_to_language_code->find(input_method_id);
  return (iter == IdMaps::GetInstance()->id_to_language_code->end()) ?
      // Returning |kDefaultLanguageCode| here is not for Chrome OS but for
      // Ubuntu where the ibus-xkb-layouts engine could be missing.
      kDefaultLanguageCode : iter->second;
}

std::string GetKeyboardLayoutName(const std::string& input_method_id) {
  if (!StartsWithASCII(input_method_id, "xkb:", true)) {
    return "";
  }

  std::vector<std::string> splitted_id;
  base::SplitString(input_method_id, ':', &splitted_id);
  return (splitted_id.size() > 1) ? splitted_id[1] : "";
}

std::string GetKeyboardOverlayId(const std::string& input_method_id) {
  const std::map<std::string, std::string>& id_map =
      *(IdMaps::GetInstance()->id_to_keyboard_overlay_id);
  std::map<std::string, std::string>::const_iterator iter =
      id_map.find(input_method_id);
  return (iter == id_map.end() ? "" : iter->second);
}

std::string GetInputMethodDisplayNameFromId(
    const std::string& input_method_id) {
  static const char kDefaultDisplayName[] = "USA";
  std::map<std::string, std::string>::const_iterator iter
      = IdMaps::GetInstance()->id_to_display_name->find(input_method_id);
  return (iter == IdMaps::GetInstance()->id_to_display_name->end()) ?
      kDefaultDisplayName : iter->second;
}

std::wstring GetLanguageDisplayNameFromCode(const std::string& language_code) {
  if (!g_browser_process) {
    return L"";
  }
  return UTF16ToWide(l10n_util::GetDisplayNameForLocale(
      language_code, g_browser_process->GetApplicationLocale(), true));
}

std::wstring GetLanguageNativeDisplayNameFromCode(
    const std::string& language_code) {
  return UTF16ToWide(l10n_util::GetDisplayNameForLocale(
      language_code, language_code, true));
}

void SortLanguageCodesByNames(std::vector<std::string>* language_codes) {
  if (!g_browser_process) {
    return;
  }
  // We should build collator outside of the comparator. We cannot have
  // scoped_ptr<> in the comparator for a subtle STL reason.
  UErrorCode error = U_ZERO_ERROR;
  icu::Locale locale(g_browser_process->GetApplicationLocale().c_str());
  scoped_ptr<icu::Collator> collator(
      icu::Collator::createInstance(locale, error));
  if (U_FAILURE(error)) {
    collator.reset();
  }
  std::sort(language_codes->begin(), language_codes->end(),
            CompareLanguageCodesByLanguageName(collator.get()));
}

void SortInputMethodIdsByNames(std::vector<std::string>* input_method_ids) {
  SortInputMethodIdsByNamesInternal(
      *(IdMaps::GetInstance()->id_to_language_code), input_method_ids);
}

void SortInputMethodIdsByNamesInternal(
    const std::map<std::string, std::string>& id_to_language_code_map,
    std::vector<std::string>* input_method_ids) {
  if (!g_browser_process) {
    return;
  }
  UErrorCode error = U_ZERO_ERROR;
  icu::Locale locale(g_browser_process->GetApplicationLocale().c_str());
  scoped_ptr<icu::Collator> collator(
      icu::Collator::createInstance(locale, error));
  if (U_FAILURE(error)) {
    collator.reset();
  }
  std::stable_sort(input_method_ids->begin(), input_method_ids->end(),
                   CompareInputMethodIdsByLanguageName(
                       collator.get(), id_to_language_code_map));
}

bool GetInputMethodIdsFromLanguageCode(
    const std::string& normalized_language_code,
    InputMethodType type,
    std::vector<std::string>* out_input_method_ids) {
  return GetInputMethodIdsFromLanguageCodeInternal(
      *IdMaps::GetInstance()->language_code_to_ids,
      normalized_language_code, type, out_input_method_ids);
}

bool GetInputMethodIdsFromLanguageCodeInternal(
    const std::multimap<std::string, std::string>& language_code_to_ids,
    const std::string& normalized_language_code,
    InputMethodType type,
    std::vector<std::string>* out_input_method_ids) {
  DCHECK(out_input_method_ids);
  out_input_method_ids->clear();

  bool result = false;
  std::pair<LanguageCodeToIdsMap::const_iterator,
      LanguageCodeToIdsMap::const_iterator> range =
      language_code_to_ids.equal_range(normalized_language_code);
  for (LanguageCodeToIdsMap::const_iterator iter = range.first;
       iter != range.second; ++iter) {
    const std::string& input_method_id = iter->second;
    if ((type == kAllInputMethods) || IsKeyboardLayout(input_method_id)) {
      out_input_method_ids->push_back(input_method_id);
      result = true;
    }
  }
  if ((type == kAllInputMethods) && !result) {
    LOG(ERROR) << "Unknown language code: " << normalized_language_code;
  }
  return result;
}

void EnableInputMethods(const std::string& language_code, InputMethodType type,
                        const std::string& initial_input_method_id) {
  std::vector<std::string> input_method_ids;
  GetInputMethodIdsFromLanguageCode(language_code, type, &input_method_ids);

  std::string keyboard = CrosLibrary::Get()->GetKeyboardLibrary()->
      GetHardwareKeyboardLayoutName();
  if (std::count(input_method_ids.begin(), input_method_ids.end(),
                 keyboard) == 0) {
    input_method_ids.push_back(keyboard);
  }
  // First, sort the vector by input method id, then by its display name.
  std::sort(input_method_ids.begin(), input_method_ids.end());
  SortInputMethodIdsByNames(&input_method_ids);

  // Update ibus-daemon setting.
  ImeConfigValue value;
  value.type = ImeConfigValue::kValueTypeStringList;
  value.string_list_value = input_method_ids;
  InputMethodLibrary* library = CrosLibrary::Get()->GetInputMethodLibrary();
  library->SetImeConfig(language_prefs::kGeneralSectionName,
                        language_prefs::kPreloadEnginesConfigName, value);
  if (!initial_input_method_id.empty()) {
    library->ChangeInputMethod(initial_input_method_id);
  }
}

void OnLocaleChanged() {
  IdMaps::GetInstance()->ReloadMaps();
}

}  // namespace input_method
}  // namespace chromeos
