// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_prefs.h"

#include "base/string_util.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/extensions/extension_pref_store.h"
#include "chrome/browser/prefs/pref_notifier.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/url_pattern.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/pref_names.h"

using base::Time;

namespace {

// Additional preferences keys

// Where an extension was installed from. (see Extension::Location)
const char kPrefLocation[] = "location";

// Enabled, disabled, killed, etc. (see Extension::State)
const char kPrefState[] = "state";

// The path to the current version's manifest file.
const char kPrefPath[] = "path";

// The dictionary containing the extension's manifest.
const char kPrefManifest[] = "manifest";

// The version number.
const char kPrefVersion[] = "manifest.version";

// Indicates if an extension is blacklisted:
const char kPrefBlacklist[] = "blacklist";

// Indicates whether to show an install warning when the user enables.
const char kExtensionDidEscalatePermissions[] = "install_warning_on_enable";

// A preference that tracks browser action toolbar configuration. This is a list
// object stored in the Preferences file. The extensions are stored by ID.
const char kExtensionToolbar[] = "extensions.toolbar";

// The key for a serialized Time value indicating the start of the day (from the
// server's perspective) an extension last included a "ping" parameter during
// its update check.
const char kLastPingDay[] = "lastpingday";

// Path for settings specific to blacklist update.
const char kExtensionsBlacklistUpdate[] = "extensions.blacklistupdate";

// Path and sub-keys for the idle install info dictionary preference.
const char kIdleInstallInfo[] = "idle_install_info";
const char kIdleInstallInfoCrxPath[] = "crx_path";
const char kIdleInstallInfoVersion[] = "version";
const char kIdleInstallInfoFetchTime[] = "fetch_time";


// A preference that, if true, will allow this extension to run in incognito
// mode.
const char kPrefIncognitoEnabled[] = "incognito";

// A preference to control whether an extension is allowed to inject script in
// pages with file URLs.
const char kPrefAllowFileAccess[] = "allowFileAccess";

// A preference set by the web store to indicate login information for
// purchased apps.
const char kWebStoreLogin[] = "extensions.webstore_login";

// A preference set by the the NTP to persist the desired launch container type
// used for apps.
const char kPrefLaunchType[] = "launchType";

// A preference determining the order of which the apps appear on the NTP.
const char kPrefAppLaunchIndex[] = "app_launcher_index";

// A preference for storing extra data sent in update checks for an extension.
const char kUpdateUrlData[] = "update_url_data";

// Whether the browser action is visible in the toolbar.
const char kBrowserActionVisible[] = "browser_action_visible";

// Preferences that hold which permissions the user has granted the extension.
// We explicitly keep track of these so that extensions can contain unknown
// permissions, for backwards compatibility reasons, and we can still prompt
// the user to accept them once recognized.
const char kPrefGrantedPermissionsAPI[] = "granted_permissions.api";
const char kPrefGrantedPermissionsHost[] = "granted_permissions.host";
const char kPrefGrantedPermissionsAll[] = "granted_permissions.full";

// A preference that indicates when an extension was installed.
const char kPrefInstallTime[] = "install_time";

// A preference that contains any extension-controlled preferences.
const char kPrefPreferences[] = "preferences";

}  // namespace

////////////////////////////////////////////////////////////////////////////////

namespace {

// TODO(asargent) - This is cleanup code for a key that was introduced into
// the extensions.settings sub-dictionary which wasn't a valid extension
// id. We can remove this in a couple of months. (See http://crbug.com/40017
// and http://crbug.com/39745 for more details).
static void CleanupBadExtensionKeys(PrefService* prefs) {
  DictionaryValue* dictionary =
      prefs->GetMutableDictionary(ExtensionPrefs::kExtensionsPref);
  std::set<std::string> bad_keys;
  for (DictionaryValue::key_iterator i = dictionary->begin_keys();
       i != dictionary->end_keys(); ++i) {
    const std::string& key_name(*i);
    if (!Extension::IdIsValid(key_name)) {
      bad_keys.insert(key_name);
    }
  }
  bool dirty = false;
  for (std::set<std::string>::iterator i = bad_keys.begin();
       i != bad_keys.end(); ++i) {
    dirty = true;
    dictionary->Remove(*i, NULL);
  }
  if (dirty)
    prefs->ScheduleSavePersistentPrefs();
}

static void ExtentToStringSet(const ExtensionExtent& host_extent,
                              std::set<std::string>* result) {
  ExtensionExtent::PatternList patterns = host_extent.patterns();
  ExtensionExtent::PatternList::const_iterator i;

  for (i = patterns.begin(); i != patterns.end(); ++i)
    result->insert(i->GetAsString());
}

}  // namespace

ExtensionPrefs::ExtensionPrefs(PrefService* prefs,
                               const FilePath& root_dir,
                               ExtensionPrefStore* pref_store)
    : prefs_(prefs),
      install_directory_(root_dir),
      pref_store_(pref_store) {
  // TODO(asargent) - Remove this in a couple of months. (See comment above
  // CleanupBadExtensionKeys).
  CleanupBadExtensionKeys(prefs_);

  MakePathsRelative();

  InitPrefStore();
}

ExtensionPrefs::~ExtensionPrefs() {}

// static
const char ExtensionPrefs::kExtensionsPref[] = "extensions.settings";

static FilePath::StringType MakePathRelative(const FilePath& parent,
                                             const FilePath& child,
                                             bool *dirty) {
  if (!parent.IsParent(child))
    return child.value();

  if (dirty)
    *dirty = true;
  FilePath::StringType retval = child.value().substr(
      parent.value().length());
  if (FilePath::IsSeparator(retval[0]))
    return retval.substr(1);
  else
    return retval;
}

void ExtensionPrefs::MakePathsRelative() {
  bool dirty = false;
  const DictionaryValue* dict = prefs_->GetMutableDictionary(kExtensionsPref);
  if (!dict || dict->empty())
    return;

  for (DictionaryValue::key_iterator i = dict->begin_keys();
       i != dict->end_keys(); ++i) {
    DictionaryValue* extension_dict;
    if (!dict->GetDictionaryWithoutPathExpansion(*i, &extension_dict))
      continue;
    int location_value;
    if (extension_dict->GetInteger(kPrefLocation, &location_value) &&
        location_value == Extension::LOAD) {
      // Unpacked extensions can have absolute paths.
      continue;
    }
    FilePath::StringType path_string;
    if (!extension_dict->GetString(kPrefPath, &path_string))
      continue;
    FilePath path(path_string);
    if (path.IsAbsolute()) {
      extension_dict->SetString(kPrefPath,
          MakePathRelative(install_directory_, path, &dirty));
    }
  }
  if (dirty)
    SavePrefsAndNotify();
}

void ExtensionPrefs::MakePathsAbsolute(DictionaryValue* dict) {
  if (!dict || dict->empty())
    return;

  for (DictionaryValue::key_iterator i = dict->begin_keys();
       i != dict->end_keys(); ++i) {
    DictionaryValue* extension_dict;
    if (!dict->GetDictionaryWithoutPathExpansion(*i, &extension_dict)) {
      NOTREACHED();
      continue;
    }

    int location_value;
    if (extension_dict->GetInteger(kPrefLocation, &location_value) &&
        location_value == Extension::LOAD) {
      // Unpacked extensions will already have absolute paths.
      continue;
    }

    FilePath::StringType path_string;
    if (!extension_dict->GetString(kPrefPath, &path_string))
      continue;

    DCHECK(!FilePath(path_string).IsAbsolute());
    extension_dict->SetString(
        kPrefPath, install_directory_.Append(path_string).value());
  }
}

DictionaryValue* ExtensionPrefs::CopyCurrentExtensions() {
  const DictionaryValue* extensions = prefs_->GetDictionary(kExtensionsPref);
  if (extensions) {
    DictionaryValue* copy =
        static_cast<DictionaryValue*>(extensions->DeepCopy());
    MakePathsAbsolute(copy);
    return copy;
  }
  return new DictionaryValue;
}

bool ExtensionPrefs::ReadBooleanFromPref(
    DictionaryValue* ext, const std::string& pref_key) {
  bool bool_value = false;
  if (!ext->GetBoolean(pref_key, &bool_value))
    return false;

  return bool_value;
}

bool ExtensionPrefs::ReadExtensionPrefBoolean(
    const std::string& extension_id, const std::string& pref_key) {
  DictionaryValue* ext = GetExtensionPref(extension_id);
  if (!ext) {
    // No such extension yet.
    return false;
  }
  return ReadBooleanFromPref(ext, pref_key);
}

bool ExtensionPrefs::ReadIntegerFromPref(
    DictionaryValue* ext, const std::string& pref_key, int* out_value) {
  if (!ext->GetInteger(pref_key, out_value))
    return false;

  return out_value != NULL;
}

bool ExtensionPrefs::ReadExtensionPrefInteger(
    const std::string& extension_id, const std::string& pref_key,
    int* out_value) {
  DictionaryValue* ext = GetExtensionPref(extension_id);
  if (!ext) {
    // No such extension yet.
    return false;
  }
  return ReadIntegerFromPref(ext, pref_key, out_value);
}

bool ExtensionPrefs::ReadExtensionPrefList(
    const std::string& extension_id, const std::string& pref_key,
    ListValue** out_value) {
  DictionaryValue* ext = GetExtensionPref(extension_id);
  if (!ext || !ext->GetList(pref_key, out_value))
    return false;

  return out_value != NULL;
}

bool ExtensionPrefs::ReadExtensionPrefStringSet(
    const std::string& extension_id,
    const std::string& pref_key,
    std::set<std::string>* result) {
  ListValue* value = NULL;
  if (!ReadExtensionPrefList(extension_id, pref_key, &value))
    return false;

  result->clear();

  for (size_t i = 0; i < value->GetSize(); ++i) {
    std::string item;
    if (!value->GetString(i, &item))
      return false;
    result->insert(item);
  }

  return true;
}

void ExtensionPrefs::AddToExtensionPrefStringSet(
    const std::string& extension_id,
    const std::string& pref_key,
    const std::set<std::string>& added_value) {
  std::set<std::string> old_value;
  std::set<std::string> new_value;
  ReadExtensionPrefStringSet(extension_id, pref_key, &old_value);

  std::set_union(old_value.begin(), old_value.end(),
                 added_value.begin(), added_value.end(),
                 std::inserter(new_value, new_value.begin()));

  ListValue* value = new ListValue();
  for (std::set<std::string>::const_iterator iter = new_value.begin();
       iter != new_value.end(); ++iter)
    value->Append(Value::CreateStringValue(*iter));

  UpdateExtensionPref(extension_id, pref_key, value);
  prefs_->ScheduleSavePersistentPrefs();
}

void ExtensionPrefs::SavePrefsAndNotify() {
  prefs_->ScheduleSavePersistentPrefs();
  // TODO(mnissler, danno): Don't use pref_notifier() here, but tell the
  // PrefService by some other means that we changed the pref value.
  prefs_->pref_notifier()->OnPreferenceChanged(kExtensionsPref);
}

bool ExtensionPrefs::IsBlacklistBitSet(DictionaryValue* ext) {
  return ReadBooleanFromPref(ext, kPrefBlacklist);
}

bool ExtensionPrefs::IsExtensionBlacklisted(const std::string& extension_id) {
  return ReadExtensionPrefBoolean(extension_id, kPrefBlacklist);
}

bool ExtensionPrefs::IsExtensionAllowedByPolicy(
    const std::string& extension_id) {
  std::string string_value;

  const ListValue* blacklist =
      prefs_->GetList(prefs::kExtensionInstallDenyList);
  if (!blacklist || blacklist->empty())
    return true;

  // Check the whitelist first.
  const ListValue* whitelist =
      prefs_->GetList(prefs::kExtensionInstallAllowList);
  if (whitelist) {
    for (ListValue::const_iterator it = whitelist->begin();
         it != whitelist->end(); ++it) {
      if (!(*it)->GetAsString(&string_value))
        LOG(WARNING) << "Failed to read whitelist string.";
      else if (string_value == extension_id)
        return true;
    }
  }

  // Then check the blacklist (the admin blacklist, not the Google blacklist).
  if (blacklist) {
    for (ListValue::const_iterator it = blacklist->begin();
         it != blacklist->end(); ++it) {
      if (!(*it)->GetAsString(&string_value)) {
        LOG(WARNING) << "Failed to read blacklist string.";
      } else {
        if (string_value == "*")
          return false;  // Only whitelisted extensions are allowed.
        if (string_value == extension_id)
          return false;
      }
    }
  }

  return true;
}

bool ExtensionPrefs::DidExtensionEscalatePermissions(
    const std::string& extension_id) {
  return ReadExtensionPrefBoolean(extension_id,
                                  kExtensionDidEscalatePermissions);
}

void ExtensionPrefs::SetDidExtensionEscalatePermissions(
    const Extension* extension, bool did_escalate) {
  UpdateExtensionPref(extension->id(), kExtensionDidEscalatePermissions,
                      Value::CreateBooleanValue(did_escalate));
  prefs_->ScheduleSavePersistentPrefs();
}

void ExtensionPrefs::UpdateBlacklist(
    const std::set<std::string>& blacklist_set) {
  std::vector<std::string> remove_pref_ids;
  std::set<std::string> used_id_set;
  const DictionaryValue* extensions = prefs_->GetDictionary(kExtensionsPref);

  if (extensions) {
    for (DictionaryValue::key_iterator extension_id = extensions->begin_keys();
         extension_id != extensions->end_keys(); ++extension_id) {
      DictionaryValue* ext;
      if (!extensions->GetDictionaryWithoutPathExpansion(*extension_id, &ext)) {
        NOTREACHED() << "Invalid pref for extension " << *extension_id;
        continue;
      }
      const std::string& id(*extension_id);
      if (blacklist_set.find(id) == blacklist_set.end()) {
        if (!IsBlacklistBitSet(ext)) {
          // This extension is not in blacklist. And it was not blacklisted
          // before.
          continue;
        } else {
          if (ext->size() == 1) {
            // We should remove the entry if the only flag here is blacklist.
            remove_pref_ids.push_back(id);
          } else {
            // Remove the blacklist bit.
            ext->Remove(kPrefBlacklist, NULL);
          }
        }
      } else {
        if (!IsBlacklistBitSet(ext)) {
          // Only set the blacklist if it was not set.
          ext->SetBoolean(kPrefBlacklist, true);
        }
        // Keep the record if this extension is already processed.
        used_id_set.insert(id);
      }
    }
  }

  // Iterate the leftovers to set blacklist in pref
  std::set<std::string>::const_iterator set_itr = blacklist_set.begin();
  for (; set_itr != blacklist_set.end(); ++set_itr) {
    if (used_id_set.find(*set_itr) == used_id_set.end()) {
      UpdateExtensionPref(*set_itr, kPrefBlacklist,
        Value::CreateBooleanValue(true));
    }
  }
  for (unsigned int i = 0; i < remove_pref_ids.size(); ++i) {
    DeleteExtensionPrefs(remove_pref_ids[i]);
  }
  SavePrefsAndNotify();
  return;
}

Time ExtensionPrefs::LastPingDayImpl(const DictionaryValue* dictionary) const {
  if (dictionary && dictionary->HasKey(kLastPingDay)) {
    std::string string_value;
    int64 value;
    dictionary->GetString(kLastPingDay, &string_value);
    if (base::StringToInt64(string_value, &value)) {
      return Time::FromInternalValue(value);
    }
  }
  return Time();
}

void ExtensionPrefs::SetLastPingDayImpl(const Time& time,
                                        DictionaryValue* dictionary) {
  if (!dictionary) {
    NOTREACHED();
    return;
  }
  std::string value = base::Int64ToString(time.ToInternalValue());
  dictionary->SetString(kLastPingDay, value);
  SavePrefsAndNotify();
}


bool ExtensionPrefs::GetGrantedPermissions(
    const std::string& extension_id,
    bool* full_access,
    std::set<std::string>* api_permissions,
    ExtensionExtent* host_extent) {
  CHECK(Extension::IdIsValid(extension_id));

  DictionaryValue* ext = GetExtensionPref(extension_id);
  if (!ext || !ext->GetBoolean(kPrefGrantedPermissionsAll, full_access))
    return false;

  ReadExtensionPrefStringSet(
      extension_id, kPrefGrantedPermissionsAPI, api_permissions);

  std::set<std::string> host_permissions;
  ReadExtensionPrefStringSet(
      extension_id, kPrefGrantedPermissionsHost, &host_permissions);

  // The granted host permissions contain hosts from the manifest's
  // "permissions" array and from the content script "matches" arrays,
  // so the URLPattern needs to accept valid schemes from both types.
  for (std::set<std::string>::iterator i = host_permissions.begin();
       i != host_permissions.end(); ++i)
    host_extent->AddPattern(URLPattern(
        Extension::kValidHostPermissionSchemes |
        UserScript::kValidUserScriptSchemes,
        *i));

  return true;
}

void ExtensionPrefs::AddGrantedPermissions(
    const std::string& extension_id,
    const bool full_access,
    const std::set<std::string>& api_permissions,
    const ExtensionExtent& host_extent) {
  CHECK(Extension::IdIsValid(extension_id));

  UpdateExtensionPref(extension_id, kPrefGrantedPermissionsAll,
                      Value::CreateBooleanValue(full_access));

  if (!api_permissions.empty()) {
    AddToExtensionPrefStringSet(
        extension_id, kPrefGrantedPermissionsAPI, api_permissions);
  }

  if (!host_extent.is_empty()) {
    std::set<std::string> host_permissions;
    ExtentToStringSet(host_extent, &host_permissions);

    AddToExtensionPrefStringSet(
        extension_id, kPrefGrantedPermissionsHost, host_permissions);
  }

  SavePrefsAndNotify();
}

Time ExtensionPrefs::LastPingDay(const std::string& extension_id) const {
  DCHECK(Extension::IdIsValid(extension_id));
  return LastPingDayImpl(GetExtensionPref(extension_id));
}

Time ExtensionPrefs::BlacklistLastPingDay() const {
  return LastPingDayImpl(prefs_->GetDictionary(kExtensionsBlacklistUpdate));
}

void ExtensionPrefs::SetLastPingDay(const std::string& extension_id,
                                    const Time& time) {
  DCHECK(Extension::IdIsValid(extension_id));
  SetLastPingDayImpl(time, GetExtensionPref(extension_id));
}

void ExtensionPrefs::SetBlacklistLastPingDay(const Time& time) {
  SetLastPingDayImpl(time,
                     prefs_->GetMutableDictionary(kExtensionsBlacklistUpdate));
}

bool ExtensionPrefs::IsIncognitoEnabled(const std::string& extension_id) {
  return ReadExtensionPrefBoolean(extension_id, kPrefIncognitoEnabled);
}

void ExtensionPrefs::SetIsIncognitoEnabled(const std::string& extension_id,
                                           bool enabled) {
  UpdateExtensionPref(extension_id, kPrefIncognitoEnabled,
                      Value::CreateBooleanValue(enabled));
  SavePrefsAndNotify();
}

bool ExtensionPrefs::AllowFileAccess(const std::string& extension_id) {
  return ReadExtensionPrefBoolean(extension_id, kPrefAllowFileAccess);
}

void ExtensionPrefs::SetAllowFileAccess(const std::string& extension_id,
                                        bool allow) {
  UpdateExtensionPref(extension_id, kPrefAllowFileAccess,
                      Value::CreateBooleanValue(allow));
  SavePrefsAndNotify();
}

ExtensionPrefs::LaunchType ExtensionPrefs::GetLaunchType(
    const std::string& extension_id,
    ExtensionPrefs::LaunchType default_pref_value) {
  int value = -1;
  LaunchType result = LAUNCH_REGULAR;

  if (ReadExtensionPrefInteger(extension_id, kPrefLaunchType, &value) &&
     (value == LAUNCH_PINNED ||
      value == LAUNCH_REGULAR ||
      value == LAUNCH_FULLSCREEN ||
      value == LAUNCH_WINDOW)) {
    result = static_cast<LaunchType>(value);
  } else {
    result = default_pref_value;
  }
  #if defined(OS_MACOSX)
    // App windows are not yet supported on mac.  Pref sync could make
    // the launch type LAUNCH_WINDOW, even if there is no UI to set it
    // on mac.
    if (result == LAUNCH_WINDOW)
      result = LAUNCH_REGULAR;
  #endif

  return result;
}

extension_misc::LaunchContainer ExtensionPrefs::GetLaunchContainer(
    const Extension* extension,
    ExtensionPrefs::LaunchType default_pref_value) {
  extension_misc::LaunchContainer launch_container =
      extension->launch_container();

  // Apps with app.launch.container = 'panel' should always
  // open in a panel.
  if (launch_container == extension_misc::LAUNCH_PANEL)
    return extension_misc::LAUNCH_PANEL;

  ExtensionPrefs::LaunchType prefs_launch_type =
      GetLaunchType(extension->id(), default_pref_value);

  // If the user chose to open in a window, then launch in one.
  if (prefs_launch_type == ExtensionPrefs::LAUNCH_WINDOW)
    return extension_misc::LAUNCH_WINDOW;

  // Otherwise, use the container the extension chose.
  return launch_container;
}

void ExtensionPrefs::SetLaunchType(const std::string& extension_id,
                                   LaunchType launch_type) {
  UpdateExtensionPref(extension_id, kPrefLaunchType,
      Value::CreateIntegerValue(static_cast<int>(launch_type)));
  SavePrefsAndNotify();
}

bool ExtensionPrefs::IsExtensionKilled(const std::string& id) {
  DictionaryValue* extension = GetExtensionPref(id);
  if (!extension)
    return false;
  int state = 0;
  return extension->GetInteger(kPrefState, &state) &&
         state == static_cast<int>(Extension::KILLBIT);
}

std::vector<std::string> ExtensionPrefs::GetToolbarOrder() {
  ExtensionPrefs::ExtensionIdSet extension_ids;
  const ListValue* toolbar_order = prefs_->GetList(kExtensionToolbar);
  if (toolbar_order) {
    for (size_t i = 0; i < toolbar_order->GetSize(); ++i) {
      std::string extension_id;
      if (toolbar_order->GetString(i, &extension_id))
        extension_ids.push_back(extension_id);
    }
  }
  return extension_ids;
}

void ExtensionPrefs::SetToolbarOrder(
    const std::vector<std::string>& extension_ids) {
  ListValue* toolbar_order = prefs_->GetMutableList(kExtensionToolbar);
  toolbar_order->Clear();
  for (std::vector<std::string>::const_iterator iter = extension_ids.begin();
       iter != extension_ids.end(); ++iter) {
    toolbar_order->Append(new StringValue(*iter));
  }
  SavePrefsAndNotify();
}

void ExtensionPrefs::OnExtensionInstalled(
    const Extension* extension, Extension::State initial_state,
    bool initial_incognito_enabled) {
  const std::string& id = extension->id();
  const base::Time install_time = GetCurrentTime();
  UpdateExtensionPref(id, kPrefState,
                      Value::CreateIntegerValue(initial_state));
  UpdateExtensionPref(id, kPrefIncognitoEnabled,
                      Value::CreateBooleanValue(initial_incognito_enabled));
  UpdateExtensionPref(id, kPrefLocation,
                      Value::CreateIntegerValue(extension->location()));
  UpdateExtensionPref(id, kPrefInstallTime,
                      Value::CreateStringValue(
                          base::Int64ToString(install_time.ToInternalValue())));
  UpdateExtensionPref(id, kPrefPreferences, new DictionaryValue());

  FilePath::StringType path = MakePathRelative(install_directory_,
      extension->path(), NULL);
  UpdateExtensionPref(id, kPrefPath, Value::CreateStringValue(path));
  // We store prefs about LOAD extensions, but don't cache their manifest
  // since it may change on disk.
  if (extension->location() != Extension::LOAD) {
    UpdateExtensionPref(id, kPrefManifest,
                        extension->manifest_value()->DeepCopy());
  }
  UpdateExtensionPref(id, kPrefAppLaunchIndex,
                      Value::CreateIntegerValue(GetNextAppLaunchIndex()));
  SavePrefsAndNotify();
}

void ExtensionPrefs::OnExtensionUninstalled(const std::string& extension_id,
                                            const Extension::Location& location,
                                            bool external_uninstall) {
  PrefKeySet pref_keys;
  GetExtensionControlledPrefKeys(extension_id, &pref_keys);

  // For external extensions, we save a preference reminding ourself not to try
  // and install the extension anymore (except when |external_uninstall| is
  // true, which signifies that the registry key was deleted or the pref file
  // no longer lists the extension).
  if (!external_uninstall && Extension::IsExternalLocation(location)) {
    UpdateExtensionPref(extension_id, kPrefState,
                        Value::CreateIntegerValue(Extension::KILLBIT));
    SavePrefsAndNotify();
  } else {
    DeleteExtensionPrefs(extension_id);
  }

  UpdatePrefStore(pref_keys);
}

Extension::State ExtensionPrefs::GetExtensionState(
    const std::string& extension_id) const {
  DictionaryValue* extension = GetExtensionPref(extension_id);

  // If the extension doesn't have a pref, it's a --load-extension.
  if (!extension)
    return Extension::ENABLED;

  int state = -1;
  if (!extension->GetInteger(kPrefState, &state) ||
      state < 0 || state >= Extension::NUM_STATES) {
    LOG(ERROR) << "Bad or missing pref 'state' for extension '"
               << extension_id << "'";
    return Extension::ENABLED;
  }
  return static_cast<Extension::State>(state);
}

void ExtensionPrefs::SetExtensionState(const Extension* extension,
                                       Extension::State state) {
  UpdateExtensionPref(extension->id(), kPrefState,
                      Value::CreateIntegerValue(state));

  PrefKeySet pref_keys;
  GetExtensionControlledPrefKeys(extension->id(), &pref_keys);
  UpdatePrefStore(pref_keys);

  SavePrefsAndNotify();
}

bool ExtensionPrefs::GetBrowserActionVisibility(const Extension* extension) {
  DictionaryValue* extension_prefs = GetExtensionPref(extension->id());
  if (!extension_prefs)
    return true;
  bool visible = false;
  if (!extension_prefs->GetBoolean(kBrowserActionVisible, &visible) || visible)
    return true;

  return false;
}

void ExtensionPrefs::SetBrowserActionVisibility(const Extension* extension,
                                                bool visible) {
  if (GetBrowserActionVisibility(extension) == visible)
    return;

  UpdateExtensionPref(extension->id(), kBrowserActionVisible,
                      Value::CreateBooleanValue(visible));
  SavePrefsAndNotify();

  NotificationService::current()->Notify(
      NotificationType::EXTENSION_BROWSER_ACTION_VISIBILITY_CHANGED,
      Source<ExtensionPrefs>(this),
      Details<const Extension>(extension));
}

std::string ExtensionPrefs::GetVersionString(const std::string& extension_id) {
  DictionaryValue* extension = GetExtensionPref(extension_id);
  if (!extension)
    return std::string();

  std::string version;
  if (!extension->GetString(kPrefVersion, &version)) {
    LOG(ERROR) << "Bad or missing pref 'version' for extension '"
               << extension_id << "'";
  }

  return version;
}

void ExtensionPrefs::UpdateManifest(const Extension* extension) {
  if (extension->location() != Extension::LOAD) {
    UpdateExtensionPref(extension->id(), kPrefManifest,
                        extension->manifest_value()->DeepCopy());
    SavePrefsAndNotify();
  }
}

FilePath ExtensionPrefs::GetExtensionPath(const std::string& extension_id) {
  const DictionaryValue* dict = prefs_->GetDictionary(kExtensionsPref);
  if (!dict || dict->empty())
    return FilePath();

  std::string path;
  if (!dict->GetString(extension_id + "." + kPrefPath, &path))
    return FilePath();

  return install_directory_.Append(FilePath::FromWStringHack(UTF8ToWide(path)));
}

void ExtensionPrefs::UpdateExtensionPref(const std::string& extension_id,
                                         const std::string& key,
                                         Value* data_value) {
  if (!Extension::IdIsValid(extension_id)) {
    NOTREACHED() << "Invalid extension_id " << extension_id;
    return;
  }
  DictionaryValue* extension = GetOrCreateExtensionPref(extension_id);
  extension->Set(key, data_value);
}

void ExtensionPrefs::DeleteExtensionPrefs(const std::string& extension_id) {
  DictionaryValue* dict = prefs_->GetMutableDictionary(kExtensionsPref);
  if (dict->HasKey(extension_id)) {
    dict->Remove(extension_id, NULL);
    SavePrefsAndNotify();
  }
}

DictionaryValue* ExtensionPrefs::GetOrCreateExtensionPref(
    const std::string& extension_id) {
  DCHECK(Extension::IdIsValid(extension_id));
  DictionaryValue* dict = prefs_->GetMutableDictionary(kExtensionsPref);
  DictionaryValue* extension = NULL;
  if (!dict->GetDictionary(extension_id, &extension)) {
    // Extension pref does not exist, create it.
    extension = new DictionaryValue();
    dict->Set(extension_id, extension);
  }
  return extension;
}

DictionaryValue* ExtensionPrefs::GetExtensionPref(
    const std::string& extension_id) const {
  const DictionaryValue* dict = prefs_->GetDictionary(kExtensionsPref);
  if (!dict)
    return NULL;
  DictionaryValue* extension = NULL;
  dict->GetDictionary(extension_id, &extension);
  return extension;
}

DictionaryValue* ExtensionPrefs::GetExtensionControlledPrefs(
    const std::string& extension_id) const {
  DictionaryValue* extension = GetExtensionPref(extension_id);
  if (!extension) {
    NOTREACHED();
    return NULL;
  }
  DictionaryValue* preferences = NULL;
  extension->GetDictionary(kPrefPreferences, &preferences);
  return preferences;
}

// Helper function for GetInstalledExtensionsInfo.
static ExtensionInfo* GetInstalledExtensionInfoImpl(
    DictionaryValue* extension_data,
    DictionaryValue::key_iterator extension_id) {
  DictionaryValue* ext;
  if (!extension_data->GetDictionaryWithoutPathExpansion(*extension_id, &ext)) {
    LOG(WARNING) << "Invalid pref for extension " << *extension_id;
    NOTREACHED();
    return NULL;
  }
  if (ext->HasKey(kPrefBlacklist)) {
    bool is_blacklisted = false;
    if (!ext->GetBoolean(kPrefBlacklist, &is_blacklisted)) {
      NOTREACHED() << "Invalid blacklist pref:" << *extension_id;
      return NULL;
    }
    if (is_blacklisted) {
      return NULL;
    }
  }
  int state_value;
  if (!ext->GetInteger(kPrefState, &state_value)) {
    // This can legitimately happen if we store preferences for component
    // extensions.
    return NULL;
  }
  if (state_value == Extension::KILLBIT) {
    LOG(WARNING) << "External extension has been uninstalled by the user "
                 << *extension_id;
    return NULL;
  }
  FilePath::StringType path;
  if (!ext->GetString(kPrefPath, &path)) {
    return NULL;
  }
  int location_value;
  if (!ext->GetInteger(kPrefLocation, &location_value)) {
    return NULL;
  }

  // Only the following extension types can be installed permanently in the
  // preferences.
  Extension::Location location =
      static_cast<Extension::Location>(location_value);
  if (location != Extension::INTERNAL &&
      location != Extension::LOAD &&
      !Extension::IsExternalLocation(location)) {
    NOTREACHED();
    return NULL;
  }

  DictionaryValue* manifest = NULL;
  if (location != Extension::LOAD &&
      !ext->GetDictionary(kPrefManifest, &manifest)) {
    LOG(WARNING) << "Missing manifest for extension " << *extension_id;
    // Just a warning for now.
  }

  return new ExtensionInfo(manifest, *extension_id, FilePath(path), location);
}

ExtensionPrefs::ExtensionsInfo* ExtensionPrefs::GetInstalledExtensionsInfo() {
  scoped_ptr<DictionaryValue> extension_data(CopyCurrentExtensions());

  ExtensionsInfo* extensions_info = new ExtensionsInfo;

  for (DictionaryValue::key_iterator extension_id(
           extension_data->begin_keys());
       extension_id != extension_data->end_keys(); ++extension_id) {
    if (!Extension::IdIsValid(*extension_id))
      continue;

    ExtensionInfo* info = GetInstalledExtensionInfoImpl(extension_data.get(),
                                                        extension_id);
    if (info)
      extensions_info->push_back(linked_ptr<ExtensionInfo>(info));
  }

  return extensions_info;
}

ExtensionInfo* ExtensionPrefs::GetInstalledExtensionInfo(
    const std::string& extension_id) {
  scoped_ptr<DictionaryValue> extension_data(CopyCurrentExtensions());

  for (DictionaryValue::key_iterator extension_iter(
           extension_data->begin_keys());
       extension_iter != extension_data->end_keys(); ++extension_iter) {
    if (*extension_iter == extension_id) {
      return GetInstalledExtensionInfoImpl(extension_data.get(),
                                           extension_iter);
    }
  }

  return NULL;
}

void ExtensionPrefs::SetIdleInstallInfo(const std::string& extension_id,
                                        const FilePath& crx_path,
                                        const std::string& version,
                                        const base::Time& fetch_time) {
  DictionaryValue* extension_prefs = GetExtensionPref(extension_id);
  if (!extension_prefs) {
    NOTREACHED();
    return;
  }
  extension_prefs->Remove(kIdleInstallInfo, NULL);
  DictionaryValue* info = new DictionaryValue();
  info->SetString(kIdleInstallInfoCrxPath, crx_path.value());
  info->SetString(kIdleInstallInfoVersion, version);
  info->SetString(kIdleInstallInfoFetchTime,
                  base::Int64ToString(fetch_time.ToInternalValue()));
  extension_prefs->Set(kIdleInstallInfo, info);
  SavePrefsAndNotify();
}

bool ExtensionPrefs::RemoveIdleInstallInfo(const std::string& extension_id) {
  DictionaryValue* extension_prefs = GetExtensionPref(extension_id);
  if (!extension_prefs)
    return false;
  bool result = extension_prefs->Remove(kIdleInstallInfo, NULL);
  SavePrefsAndNotify();
  return result;
}

bool ExtensionPrefs::GetIdleInstallInfo(const std::string& extension_id,
                                        FilePath* crx_path,
                                        std::string* version,
                                        base::Time* fetch_time) {
  DictionaryValue* extension_prefs = GetExtensionPref(extension_id);
  if (!extension_prefs)
    return false;

  // Do all the reads from the prefs together, and don't do any assignment
  // to the out parameters unless all the reads succeed.
  DictionaryValue* info = NULL;
  if (!extension_prefs->GetDictionary(kIdleInstallInfo, &info))
    return false;

  FilePath::StringType path_string;
  if (!info->GetString(kIdleInstallInfoCrxPath, &path_string))
    return false;

  std::string tmp_version;
  if (!info->GetString(kIdleInstallInfoVersion, &tmp_version))
    return false;

  std::string fetch_time_string;
  if (!info->GetString(kIdleInstallInfoFetchTime, &fetch_time_string))
    return false;

  int64 fetch_time_value;
  if (!base::StringToInt64(fetch_time_string, &fetch_time_value))
    return false;

  if (crx_path)
    *crx_path = FilePath(path_string);

  if (version)
    *version = tmp_version;

  if (fetch_time)
    *fetch_time = base::Time::FromInternalValue(fetch_time_value);

  return true;
}

std::set<std::string> ExtensionPrefs::GetIdleInstallInfoIds() {
  std::set<std::string> result;

  const DictionaryValue* extensions = prefs_->GetDictionary(kExtensionsPref);
  if (!extensions)
    return result;

  for (DictionaryValue::key_iterator iter = extensions->begin_keys();
       iter != extensions->end_keys(); ++iter) {
    const std::string& id(*iter);
    if (!Extension::IdIsValid(id)) {
      NOTREACHED();
      continue;
    }

    DictionaryValue* extension_prefs = GetExtensionPref(id);
    if (!extension_prefs)
      continue;

    DictionaryValue* info = NULL;
    if (extension_prefs->GetDictionary(kIdleInstallInfo, &info))
      result.insert(id);
  }
  return result;
}

bool ExtensionPrefs::GetWebStoreLogin(std::string* result) {
  if (prefs_->HasPrefPath(kWebStoreLogin)) {
    *result = prefs_->GetString(kWebStoreLogin);
    return true;
  }
  return false;
}

void ExtensionPrefs::SetWebStoreLogin(const std::string& login) {
  prefs_->SetString(kWebStoreLogin, login);
  SavePrefsAndNotify();
}

int ExtensionPrefs::GetAppLaunchIndex(const std::string& extension_id) {
  int value;
  if (ReadExtensionPrefInteger(extension_id, kPrefAppLaunchIndex, &value))
    return value;

  return -1;
}

void ExtensionPrefs::SetAppLaunchIndex(const std::string& extension_id,
                                       int index) {
  DCHECK_GE(index, 0);
  UpdateExtensionPref(extension_id, kPrefAppLaunchIndex,
                      Value::CreateIntegerValue(index));
  SavePrefsAndNotify();
}

int ExtensionPrefs::GetNextAppLaunchIndex() {
  const DictionaryValue* extensions = prefs_->GetDictionary(kExtensionsPref);
  if (!extensions)
    return 0;

  int max_value = -1;
  for (DictionaryValue::key_iterator extension_id = extensions->begin_keys();
       extension_id != extensions->end_keys(); ++extension_id) {
    int value = GetAppLaunchIndex(*extension_id);
    if (value > max_value)
      max_value = value;
  }
  return max_value + 1;
}

void ExtensionPrefs::SetUpdateUrlData(const std::string& extension_id,
                                      const std::string& data) {
  DictionaryValue* dictionary = GetExtensionPref(extension_id);
  if (!dictionary) {
    NOTREACHED();
    return;
  }

  dictionary->SetString(kUpdateUrlData, data);
  SavePrefsAndNotify();
}

std::string ExtensionPrefs::GetUpdateUrlData(const std::string& extension_id) {
  DictionaryValue* dictionary = GetExtensionPref(extension_id);
  if (!dictionary)
    return std::string();

  std::string data;
  dictionary->GetString(kUpdateUrlData, &data);
  return data;
}

base::Time ExtensionPrefs::GetCurrentTime() const {
  return base::Time::Now();
}

base::Time ExtensionPrefs::GetInstallTime(
    const std::string& extension_id) const {
  const DictionaryValue* extension = GetExtensionPref(extension_id);
  if (!extension) {
    NOTREACHED();
    return base::Time();
  }
  std::string install_time_str("0");
  extension->GetString(kPrefInstallTime, &install_time_str);
  int64 install_time_i64 = 0;
  base::StringToInt64(install_time_str, &install_time_i64);
  LOG_IF(ERROR, install_time_i64 == 0)
      << "Error parsing installation time of an extension.";
  return base::Time::FromInternalValue(install_time_i64);
}

void ExtensionPrefs::GetEnabledExtensions(ExtensionIdSet* out) const {
  CHECK(out);
  const DictionaryValue* extensions =
      pref_service()->GetDictionary(kExtensionsPref);

  for (DictionaryValue::key_iterator ext_id = extensions->begin_keys();
       ext_id != extensions->end_keys(); ++ext_id) {
    if (GetExtensionState(*ext_id) != Extension::ENABLED)
      continue;
    out->push_back(*ext_id);
  }
}

void ExtensionPrefs::FixMissingPrefs(const ExtensionIdSet& extension_ids) {
  // Fix old entries that did not get an installation time entry when they
  // were installed or don't have a preferences field.
  bool persist_required = false;
  for (ExtensionIdSet::const_iterator ext_id = extension_ids.begin();
       ext_id != extension_ids.end(); ++ext_id) {
    DictionaryValue* extension = GetExtensionPref(*ext_id);
    CHECK(extension);

    if (GetInstallTime(*ext_id) == base::Time()) {
      const base::Time install_time = GetCurrentTime();
      extension->Set(kPrefInstallTime,
                     Value::CreateStringValue(
                         base::Int64ToString(install_time.ToInternalValue())));
      persist_required = true;
    }
  }
  if (persist_required)
    SavePrefsAndNotify();
}

void ExtensionPrefs::InitPrefStore() {
  // When this is called, the PrefService is initialized and provides access
  // to the user preferences stored in a JSON file.
  ExtensionIdSet extension_ids;
  GetEnabledExtensions(&extension_ids);
  FixMissingPrefs(extension_ids);

  // Collect the unique extension controlled preference keys of all extensions.
  PrefKeySet ext_controlled_prefs;
  for (ExtensionIdSet::iterator ext_id = extension_ids.begin();
       ext_id != extension_ids.end(); ++ext_id) {
    GetExtensionControlledPrefKeys(*ext_id, &ext_controlled_prefs);
  }

  // Store winning preference for each extension controlled preference.
  UpdatePrefStore(ext_controlled_prefs);
  pref_store_->OnInitializationCompleted();
}

const Value* ExtensionPrefs::GetWinningExtensionControlledPrefValue(
    const std::string& key) const {
  Value *winner = NULL;
  base::Time winners_install_time = base::Time();

  ExtensionIdSet extension_ids;
  GetEnabledExtensions(&extension_ids);
  for (ExtensionIdSet::iterator ext_id = extension_ids.begin();
       ext_id != extension_ids.end(); ++ext_id) {
    base::Time extension_install_time = GetInstallTime(*ext_id);

    // We do not need to consider extensions that were installed before the
    // most recent extension found that provides the requested preference.
    if (extension_install_time < winners_install_time)
      continue;

    DictionaryValue* preferences = GetExtensionControlledPrefs(*ext_id);
    Value *value = NULL;
    if (preferences && preferences->GetWithoutPathExpansion(key, &value)) {
      // This extension is more recent than the last one providing this pref.
      winner = value;
      winners_install_time = extension_install_time;
    }
  }

  return winner;
}

void ExtensionPrefs::UpdatePrefStore(
    const ExtensionPrefs::PrefKeySet& pref_keys) {
  for (PrefKeySet::const_iterator i = pref_keys.begin();
       i != pref_keys.end(); ++i) {
    UpdatePrefStore(*i);
  }
}

void ExtensionPrefs::UpdatePrefStore(const std::string& pref_key) {
  if (pref_store_ == NULL)
    return;
  const Value* winning_pref_value =
      GetWinningExtensionControlledPrefValue(pref_key);

  if (winning_pref_value)
    pref_store_->SetExtensionPref(pref_key, winning_pref_value->DeepCopy());
  else
    pref_store_->RemoveExtensionPref(pref_key);
}

void ExtensionPrefs::SetExtensionControlledPref(const std::string& extension_id,
                                                const std::string& pref_key,
                                                Value* value) {
  scoped_ptr<Value> scoped_value(value);
  DCHECK(pref_service()->FindPreference(pref_key.c_str()))
      << "Extension controlled preference key " << pref_key
      << " not registered.";
  DictionaryValue* extension_preferences =
      GetExtensionControlledPrefs(extension_id);

  if (extension_preferences == NULL) {  // May be pruned when writing to disk.
    DictionaryValue* extension = GetExtensionPref(extension_id);
    if (extension == NULL) {
      LOG(ERROR) << "Extension preference for " << extension_id << " undefined";
      return;
    }
    extension_preferences = new DictionaryValue;
    extension->Set(kPrefPreferences, extension_preferences);
  }

  Value* oldValue = NULL;
  extension_preferences->GetWithoutPathExpansion(pref_key, &oldValue);
  bool modified = !Value::Equals(oldValue, scoped_value.get());
  if (!modified)
    return;

  if (scoped_value.get() == NULL)
    extension_preferences->RemoveWithoutPathExpansion(pref_key, NULL);
  else
    extension_preferences->SetWithoutPathExpansion(pref_key,
                                                   scoped_value.release());
  pref_service()->ScheduleSavePersistentPrefs();

  UpdatePrefStore(pref_key);
}

void ExtensionPrefs::GetExtensionControlledPrefKeys(
    const std::string& extension_id, PrefKeySet *out) const {
  DCHECK(out != NULL);
  DictionaryValue* ext_prefs = GetExtensionControlledPrefs(extension_id);
  if (ext_prefs) {
    for (DictionaryValue::key_iterator i = ext_prefs->begin_keys();
         i != ext_prefs->end_keys(); ++i) {
      out->insert(*i);
    }
  }
}

// static
void ExtensionPrefs::RegisterUserPrefs(PrefService* prefs) {
  prefs->RegisterDictionaryPref(kExtensionsPref);
  prefs->RegisterListPref(kExtensionToolbar);
  prefs->RegisterIntegerPref(prefs::kExtensionToolbarSize, -1);
  prefs->RegisterDictionaryPref(kExtensionsBlacklistUpdate);
  prefs->RegisterListPref(prefs::kExtensionInstallAllowList);
  prefs->RegisterListPref(prefs::kExtensionInstallDenyList);
  prefs->RegisterListPref(prefs::kExtensionInstallForceList);
  prefs->RegisterStringPref(kWebStoreLogin, std::string() /* default_value */);
}
