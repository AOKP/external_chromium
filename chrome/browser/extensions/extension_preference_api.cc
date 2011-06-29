// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_preference_api.h"

#include "base/singleton.h"
#include "base/stringprintf.h"
#include "base/values.h"
#include "chrome/browser/extensions/extension_prefs.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"

namespace {

struct PrefMappingEntry {
  const char* extension_pref;
  const char* browser_pref;
  const char* permission;
};

const char kNotControllable[] = "NotControllable";
const char kControlledByOtherExtensions[] = "ControlledByOtherExtensions";
const char kControllableByThisExtension[] = "ControllableByThisExtension";
const char kControlledByThisExtension[] = "ControlledByThisExtension";

PrefMappingEntry pref_mapping[] = {
  { "blockThirdPartyCookies",
    prefs::kBlockThirdPartyCookies,
    Extension::kContentSettingsPermission
  },
  { "proxy",
    prefs::kProxy,
    Extension::kProxyPermission
  },
};

class PrefMapping {
 public:
  static PrefMapping* GetInstance() {
    return Singleton<PrefMapping>::get();
  }

  bool FindBrowserPrefForExtensionPref(const std::string& extension_pref,
                                       std::string* browser_pref,
                                       std::string* permission) {
    std::map<std::string, std::pair<std::string, std::string> >::iterator it =
        mapping_.find(extension_pref);
    if (it != mapping_.end()) {
      *browser_pref = it->second.first;
      *permission = it->second.second;
      return true;
    }
    return false;
  }

 private:
  friend struct DefaultSingletonTraits<PrefMapping>;

  std::map<std::string, std::pair<std::string, std::string> > mapping_;

  PrefMapping() {
    for (size_t i = 0; i < arraysize(pref_mapping); ++i) {
      mapping_[pref_mapping[i].extension_pref] =
          std::make_pair(pref_mapping[i].browser_pref,
                         pref_mapping[i].permission);
    }
  }
};

const char kPermissionErrorMessage[] =
    "You do not have permission to access the preference '%s'. "
    "Be sure to declare in your manifest what permissions you need.";

}  // namespace

// TODO(battre): Factor out common parts once this is stable.

GetPreferenceFunction::~GetPreferenceFunction() { }

const char* GetPreferenceFunction::GetLevelOfControl(
    const std::string& browser_pref,
    bool incognito) const {
  PrefService* prefs = incognito ? profile_->GetOffTheRecordPrefs()
                                 : profile_->GetPrefs();
  const PrefService::Preference* pref =
      prefs->FindPreference(browser_pref.c_str());
  CHECK(pref);
  ExtensionPrefs* ep = profile_->GetExtensionService()->extension_prefs();

  if (!pref->IsExtensionModifiable())
    return kNotControllable;

  if (ep->DoesExtensionControlPref(extension_id(), browser_pref, incognito))
    return kControlledByThisExtension;

  if (ep->CanExtensionControlPref(extension_id(), browser_pref, incognito))
    return kControllableByThisExtension;

  return kControlledByOtherExtensions;
}

bool GetPreferenceFunction::RunImpl() {
  std::string pref_key;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &pref_key));
  DictionaryValue* details = NULL;
  EXTENSION_FUNCTION_VALIDATE(args_->GetDictionary(1, &details));

  bool incognito = false;
  if (details->HasKey("incognito"))
    EXTENSION_FUNCTION_VALIDATE(details->GetBoolean("incognito", &incognito));

  PrefService* prefs = incognito ? profile_->GetOffTheRecordPrefs()
                                 : profile_->GetPrefs();
  std::string browser_pref;
  std::string permission;
  EXTENSION_FUNCTION_VALIDATE(
      PrefMapping::GetInstance()->FindBrowserPrefForExtensionPref(
          pref_key, &browser_pref, &permission));
  if (!GetExtension()->HasApiPermission(permission)) {
    error_ = base::StringPrintf(kPermissionErrorMessage, pref_key.c_str());
    return false;
  }

  const PrefService::Preference* pref =
      prefs->FindPreference(browser_pref.c_str());
  CHECK(pref);
  std::string level_of_control = GetLevelOfControl(browser_pref, incognito);

  scoped_ptr<DictionaryValue> result(new DictionaryValue);
  result->Set("value", pref->GetValue()->DeepCopy());
  result->Set("levelOfControl", Value::CreateStringValue(level_of_control));
  result_.reset(result.release());
  return true;
}

SetPreferenceFunction::~SetPreferenceFunction() { }

bool SetPreferenceFunction::RunImpl() {
  std::string pref_key;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &pref_key));
  DictionaryValue* details = NULL;
  EXTENSION_FUNCTION_VALIDATE(args_->GetDictionary(1, &details));

  Value* value = NULL;
  EXTENSION_FUNCTION_VALIDATE(details->Get("value", &value));

  bool incognito = false;
  if (details->HasKey("incognito"))
    EXTENSION_FUNCTION_VALIDATE(details->GetBoolean("incognito", &incognito));

  std::string browser_pref;
  std::string permission;
  EXTENSION_FUNCTION_VALIDATE(
      PrefMapping::GetInstance()->FindBrowserPrefForExtensionPref(
          pref_key, &browser_pref, &permission));
  if (!GetExtension()->HasApiPermission(permission)) {
    error_ = base::StringPrintf(kPermissionErrorMessage, pref_key.c_str());
    return false;
  }
  ExtensionPrefs* prefs = profile_->GetExtensionService()->extension_prefs();
  const PrefService::Preference* pref =
      prefs->pref_service()->FindPreference(browser_pref.c_str());
  CHECK(pref);
  EXTENSION_FUNCTION_VALIDATE(value->GetType() == pref->GetType());
  prefs->SetExtensionControlledPref(extension_id(),
                                    browser_pref,
                                    incognito,
                                    value->DeepCopy());
  return true;
}

ClearPreferenceFunction::~ClearPreferenceFunction() { }

bool ClearPreferenceFunction::RunImpl() {
  std::string pref_key;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &pref_key));
  DictionaryValue* details = NULL;
  EXTENSION_FUNCTION_VALIDATE(args_->GetDictionary(1, &details));

  bool incognito = false;
  if (details->HasKey("incognito"))
    EXTENSION_FUNCTION_VALIDATE(details->GetBoolean("incognito", &incognito));

  std::string browser_pref;
  std::string permission;
  EXTENSION_FUNCTION_VALIDATE(
      PrefMapping::GetInstance()->FindBrowserPrefForExtensionPref(
          pref_key, &browser_pref, &permission));
  if (!GetExtension()->HasApiPermission(permission)) {
    error_ = base::StringPrintf(kPermissionErrorMessage, pref_key.c_str());
    return false;
  }
  ExtensionPrefs* prefs = profile_->GetExtensionService()->extension_prefs();
  prefs->RemoveExtensionControlledPref(extension_id(), browser_pref, incognito);
  return true;
}
