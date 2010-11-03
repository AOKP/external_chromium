// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/configuration_policy_pref_store.h"

#include "base/logging.h"
#include "base/path_service.h"
#include "base/singleton.h"
#include "base/string16.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/policy/configuration_policy_provider.h"
#if defined(OS_WIN)
#include "chrome/browser/policy/configuration_policy_provider_win.h"
#elif defined(OS_MACOSX)
#include "chrome/browser/policy/configuration_policy_provider_mac.h"
#elif defined(OS_POSIX)
#include "chrome/browser/policy/config_dir_policy_provider.h"
#endif
#include "chrome/browser/policy/dummy_configuration_policy_provider.h"
#include "chrome/browser/search_engines/search_terms_data.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/policy_constants.h"
#include "chrome/common/pref_names.h"

namespace policy {

// Manages the lifecycle of the shared platform-specific policy providers
// for managed and recommended policy. Instantiated as a Singleton.
class ConfigurationPolicyProviderKeeper {
 public:
  ConfigurationPolicyProviderKeeper()
      : managed_provider_(CreateManagedProvider()),
        recommended_provider_(CreateRecommendedProvider()) {
  }
  virtual ~ConfigurationPolicyProviderKeeper() {}

  ConfigurationPolicyProvider* managed_provider() const {
    return managed_provider_.get();
  }

  ConfigurationPolicyProvider* recommended_provider() const {
    return recommended_provider_.get();
  }

 private:
  scoped_ptr<ConfigurationPolicyProvider> managed_provider_;
  scoped_ptr<ConfigurationPolicyProvider> recommended_provider_;

  static ConfigurationPolicyProvider* CreateManagedProvider() {
    const ConfigurationPolicyProvider::StaticPolicyValueMap& policy_map =
        ConfigurationPolicyPrefStore::GetChromePolicyValueMap();
#if defined(OS_WIN)
    return new ConfigurationPolicyProviderWin(policy_map);
#elif defined(OS_MACOSX)
    return new ConfigurationPolicyProviderMac(policy_map);
#elif defined(OS_POSIX)
    FilePath config_dir_path;
    if (PathService::Get(chrome::DIR_POLICY_FILES, &config_dir_path)) {
      return new ConfigDirPolicyProvider(policy_map,
          config_dir_path.Append(FILE_PATH_LITERAL("managed")));
    } else {
      return new DummyConfigurationPolicyProvider(policy_map);
    }
#else
    return new DummyConfigurationPolicyProvider(policy_map);
#endif
  }

  static ConfigurationPolicyProvider* CreateRecommendedProvider() {
    const ConfigurationPolicyProvider::StaticPolicyValueMap& policy_map =
        ConfigurationPolicyPrefStore::GetChromePolicyValueMap();
#if defined(OS_POSIX) && !defined(OS_MACOSX)
    FilePath config_dir_path;
    if (PathService::Get(chrome::DIR_POLICY_FILES, &config_dir_path)) {
      return new ConfigDirPolicyProvider(policy_map,
          config_dir_path.Append(FILE_PATH_LITERAL("recommended")));
    } else {
      return new DummyConfigurationPolicyProvider(policy_map);
    }
#else
    return new DummyConfigurationPolicyProvider(policy_map);
#endif
  }

  DISALLOW_COPY_AND_ASSIGN(ConfigurationPolicyProviderKeeper);
};

const ConfigurationPolicyPrefStore::PolicyToPreferenceMapEntry
    ConfigurationPolicyPrefStore::simple_policy_map_[] = {
  { Value::TYPE_STRING, kPolicyHomePage,  prefs::kHomePage },
  { Value::TYPE_BOOLEAN, kPolicyHomepageIsNewTabPage,
      prefs::kHomePageIsNewTabPage },
  { Value::TYPE_INTEGER, kPolicyRestoreOnStartup,
      prefs::kRestoreOnStartup},
  { Value::TYPE_LIST, kPolicyURLsToRestoreOnStartup,
      prefs::kURLsToRestoreOnStartup },
  { Value::TYPE_BOOLEAN, kPolicyAlternateErrorPagesEnabled,
      prefs::kAlternateErrorPagesEnabled },
  { Value::TYPE_BOOLEAN, kPolicySearchSuggestEnabled,
      prefs::kSearchSuggestEnabled },
  { Value::TYPE_BOOLEAN, kPolicyDnsPrefetchingEnabled,
      prefs::kDnsPrefetchingEnabled },
  { Value::TYPE_BOOLEAN, kPolicyDisableSpdy,
      prefs::kDisableSpdy },
  { Value::TYPE_BOOLEAN, kPolicySafeBrowsingEnabled,
      prefs::kSafeBrowsingEnabled },
  { Value::TYPE_BOOLEAN, kPolicyPasswordManagerEnabled,
      prefs::kPasswordManagerEnabled },
  { Value::TYPE_BOOLEAN, kPolicyPasswordManagerAllowShowPasswords,
      prefs::kPasswordManagerAllowShowPasswords },
  { Value::TYPE_BOOLEAN, kPolicyPrintingEnabled,
      prefs::kPrintingEnabled },
  { Value::TYPE_BOOLEAN, kPolicyMetricsReportingEnabled,
      prefs::kMetricsReportingEnabled },
  { Value::TYPE_STRING, kPolicyApplicationLocale,
      prefs::kApplicationLocale},
  { Value::TYPE_LIST, kPolicyExtensionInstallAllowList,
      prefs::kExtensionInstallAllowList},
  { Value::TYPE_LIST, kPolicyExtensionInstallDenyList,
      prefs::kExtensionInstallDenyList},
  { Value::TYPE_LIST, kPolicyDisabledPlugins,
      prefs::kPluginsPluginsBlacklist},
  { Value::TYPE_BOOLEAN, kPolicyShowHomeButton,
      prefs::kShowHomeButton },
  { Value::TYPE_BOOLEAN, kPolicyJavascriptEnabled,
      prefs::kWebKitJavascriptEnabled },
  { Value::TYPE_BOOLEAN, kPolicySavingBrowserHistoryDisabled,
      prefs::kSavingBrowserHistoryDisabled },

#if defined(OS_CHROMEOS)
  { Value::TYPE_BOOLEAN, kPolicyChromeOsLockOnIdleSuspend,
      prefs::kEnableScreenLock },
#endif
};

const ConfigurationPolicyPrefStore::PolicyToPreferenceMapEntry
    ConfigurationPolicyPrefStore::default_search_policy_map_[] = {
  { Value::TYPE_BOOLEAN, kPolicyDefaultSearchProviderEnabled,
      prefs::kDefaultSearchProviderEnabled },
  { Value::TYPE_STRING, kPolicyDefaultSearchProviderName,
      prefs::kDefaultSearchProviderName },
  { Value::TYPE_STRING, kPolicyDefaultSearchProviderKeyword,
      prefs::kDefaultSearchProviderKeyword },
  { Value::TYPE_STRING, kPolicyDefaultSearchProviderSearchURL,
      prefs::kDefaultSearchProviderSearchURL },
  { Value::TYPE_STRING, kPolicyDefaultSearchProviderSuggestURL,
      prefs::kDefaultSearchProviderSuggestURL },
  { Value::TYPE_STRING, kPolicyDefaultSearchProviderIconURL,
      prefs::kDefaultSearchProviderIconURL },
  { Value::TYPE_STRING, kPolicyDefaultSearchProviderEncodings,
      prefs::kDefaultSearchProviderEncodings },
};

const ConfigurationPolicyPrefStore::PolicyToPreferenceMapEntry
    ConfigurationPolicyPrefStore::proxy_policy_map_[] = {
  { Value::TYPE_STRING, kPolicyProxyServer, prefs::kProxyServer },
  { Value::TYPE_STRING, kPolicyProxyPacUrl, prefs::kProxyPacUrl },
  { Value::TYPE_STRING, kPolicyProxyBypassList, prefs::kProxyBypassList }
};

/* static */
ConfigurationPolicyProvider::StaticPolicyValueMap
ConfigurationPolicyPrefStore::GetChromePolicyValueMap() {
  static ConfigurationPolicyProvider::StaticPolicyValueMap::Entry entries[] = {
    { ConfigurationPolicyStore::kPolicyHomePage,
        Value::TYPE_STRING, key::kHomepageLocation },
    { ConfigurationPolicyStore::kPolicyHomepageIsNewTabPage,
        Value::TYPE_BOOLEAN, key::kHomepageIsNewTabPage },
    { ConfigurationPolicyStore::kPolicyRestoreOnStartup,
        Value::TYPE_INTEGER, key::kRestoreOnStartup },
    { ConfigurationPolicyStore::kPolicyURLsToRestoreOnStartup,
        Value::TYPE_LIST, key::kURLsToRestoreOnStartup },
    { ConfigurationPolicyStore::kPolicyDefaultSearchProviderEnabled,
        Value::TYPE_BOOLEAN, key::kDefaultSearchProviderEnabled },
    { ConfigurationPolicyStore::kPolicyDefaultSearchProviderName,
        Value::TYPE_STRING, key::kDefaultSearchProviderName },
    { ConfigurationPolicyStore::kPolicyDefaultSearchProviderKeyword,
        Value::TYPE_STRING, key::kDefaultSearchProviderKeyword },
    { ConfigurationPolicyStore::kPolicyDefaultSearchProviderSearchURL,
        Value::TYPE_STRING, key::kDefaultSearchProviderSearchURL },
    { ConfigurationPolicyStore::kPolicyDefaultSearchProviderSuggestURL,
        Value::TYPE_STRING, key::kDefaultSearchProviderSuggestURL },
    { ConfigurationPolicyStore::kPolicyDefaultSearchProviderIconURL,
        Value::TYPE_STRING, key::kDefaultSearchProviderIconURL },
    { ConfigurationPolicyStore::kPolicyDefaultSearchProviderEncodings,
        Value::TYPE_STRING, key::kDefaultSearchProviderEncodings },
    { ConfigurationPolicyStore::kPolicyProxyServerMode,
        Value::TYPE_INTEGER, key::kProxyServerMode },
    { ConfigurationPolicyStore::kPolicyProxyServer,
        Value::TYPE_STRING, key::kProxyServer },
    { ConfigurationPolicyStore::kPolicyProxyPacUrl,
        Value::TYPE_STRING, key::kProxyPacUrl },
    { ConfigurationPolicyStore::kPolicyProxyBypassList,
        Value::TYPE_STRING, key::kProxyBypassList },
    { ConfigurationPolicyStore::kPolicyAlternateErrorPagesEnabled,
        Value::TYPE_BOOLEAN, key::kAlternateErrorPagesEnabled },
    { ConfigurationPolicyStore::kPolicySearchSuggestEnabled,
        Value::TYPE_BOOLEAN, key::kSearchSuggestEnabled },
    { ConfigurationPolicyStore::kPolicyDnsPrefetchingEnabled,
        Value::TYPE_BOOLEAN, key::kDnsPrefetchingEnabled },
    { ConfigurationPolicyStore::kPolicyDisableSpdy,
        Value::TYPE_BOOLEAN, key::kDisableSpdy },
    { ConfigurationPolicyStore::kPolicySafeBrowsingEnabled,
        Value::TYPE_BOOLEAN, key::kSafeBrowsingEnabled },
    { ConfigurationPolicyStore::kPolicyMetricsReportingEnabled,
        Value::TYPE_BOOLEAN, key::kMetricsReportingEnabled },
    { ConfigurationPolicyStore::kPolicyPasswordManagerEnabled,
        Value::TYPE_BOOLEAN, key::kPasswordManagerEnabled },
    { ConfigurationPolicyStore::kPolicyPasswordManagerAllowShowPasswords,
        Value::TYPE_BOOLEAN, key::kPasswordManagerAllowShowPasswords },
    { ConfigurationPolicyStore::kPolicyAutoFillEnabled,
        Value::TYPE_BOOLEAN, key::kAutoFillEnabled },
    { ConfigurationPolicyStore::kPolicyDisabledPlugins,
        Value::TYPE_LIST, key::kDisabledPlugins },
    { ConfigurationPolicyStore::kPolicyApplicationLocale,
        Value::TYPE_STRING, key::kApplicationLocaleValue },
    { ConfigurationPolicyStore::kPolicySyncDisabled,
        Value::TYPE_BOOLEAN, key::kSyncDisabled },
    { ConfigurationPolicyStore::kPolicyExtensionInstallAllowList,
        Value::TYPE_LIST, key::kExtensionInstallAllowList },
    { ConfigurationPolicyStore::kPolicyExtensionInstallDenyList,
        Value::TYPE_LIST, key::kExtensionInstallDenyList },
    { ConfigurationPolicyStore::kPolicyShowHomeButton,
        Value::TYPE_BOOLEAN, key::kShowHomeButton },
    { ConfigurationPolicyStore::kPolicyPrintingEnabled,
        Value::TYPE_BOOLEAN, key::kPrintingEnabled },
    { ConfigurationPolicyStore::kPolicyJavascriptEnabled,
        Value::TYPE_BOOLEAN, key::kJavascriptEnabled },
    { ConfigurationPolicyStore::kPolicySavingBrowserHistoryDisabled,
        Value::TYPE_BOOLEAN, key::kSavingBrowserHistoryDisabled },

#if defined(OS_CHROMEOS)
    { ConfigurationPolicyStore::kPolicyChromeOsLockOnIdleSuspend,
        Value::TYPE_BOOLEAN, key::kChromeOsLockOnIdleSuspend },
#endif
  };

  ConfigurationPolicyProvider::StaticPolicyValueMap map = {
    arraysize(entries),
    entries
  };
  return map;
}

void ConfigurationPolicyPrefStore::GetProxyPreferenceSet(
    ProxyPreferenceSet* proxy_pref_set) {
  proxy_pref_set->clear();
  for (size_t current = 0; current < arraysize(proxy_policy_map_); ++current) {
    proxy_pref_set->insert(proxy_policy_map_[current].preference_path);
  }
  proxy_pref_set->insert(prefs::kNoProxyServer);
  proxy_pref_set->insert(prefs::kProxyAutoDetect);
}

ConfigurationPolicyPrefStore::ConfigurationPolicyPrefStore(
    ConfigurationPolicyProvider* provider)
    : provider_(provider),
      prefs_(new DictionaryValue()),
      lower_priority_proxy_settings_overridden_(false),
      proxy_disabled_(false),
      proxy_configuration_specified_(false),
      use_system_proxy_(false) {
}

PrefStore::PrefReadError ConfigurationPolicyPrefStore::ReadPrefs() {
  proxy_disabled_ = false;
  proxy_configuration_specified_ = false;
  lower_priority_proxy_settings_overridden_ = false;

  bool success = (provider_ == NULL || provider_->Provide(this));
  FinalizeDefaultSearchPolicySettings();
  return success ? PrefStore::PREF_READ_ERROR_NONE :
                   PrefStore::PREF_READ_ERROR_OTHER;
}

void ConfigurationPolicyPrefStore::Apply(PolicyType policy, Value* value) {
  if (ApplyProxyPolicy(policy, value))
    return;

  if (ApplySyncPolicy(policy, value))
    return;

  if (ApplyAutoFillPolicy(policy, value))
    return;

  if (ApplyPolicyFromMap(policy, value, default_search_policy_map_,
                         arraysize(default_search_policy_map_)))
    return;

  if (ApplyPolicyFromMap(policy, value, simple_policy_map_,
                         arraysize(simple_policy_map_)))
    return;

  // Other policy implementations go here.
  NOTIMPLEMENTED();
  delete value;
}

// static
ConfigurationPolicyPrefStore*
ConfigurationPolicyPrefStore::CreateManagedPolicyPrefStore() {
  ConfigurationPolicyProviderKeeper* keeper =
      Singleton<ConfigurationPolicyProviderKeeper>::get();
  return new ConfigurationPolicyPrefStore(keeper->managed_provider());
}

// static
ConfigurationPolicyPrefStore*
ConfigurationPolicyPrefStore::CreateRecommendedPolicyPrefStore() {
  ConfigurationPolicyProviderKeeper* keeper =
      Singleton<ConfigurationPolicyProviderKeeper>::get();
  return new ConfigurationPolicyPrefStore(keeper->recommended_provider());
}

const ConfigurationPolicyPrefStore::PolicyToPreferenceMapEntry*
ConfigurationPolicyPrefStore::FindPolicyInMap(PolicyType policy,
    const PolicyToPreferenceMapEntry* map, int table_size) {
  for (int i = 0; i < table_size; ++i) {
    if (map[i].policy_type == policy)
      return map + i;
  }
  return NULL;
}

bool ConfigurationPolicyPrefStore::RemovePreferencesOfMap(
    const PolicyToPreferenceMapEntry* map, int table_size) {
  bool found_one = false;
  for (int i = 0; i < table_size; ++i) {
    if (prefs_->Remove(map[i].preference_path, NULL))
      found_one = true;
  }
  return found_one;
}

bool ConfigurationPolicyPrefStore::ApplyPolicyFromMap(PolicyType policy,
    Value* value, const PolicyToPreferenceMapEntry map[], int size) {
  const PolicyToPreferenceMapEntry* end = map + size;
  for (const PolicyToPreferenceMapEntry* current = map;
       current != end; ++current) {
    if (current->policy_type == policy) {
      DCHECK(current->value_type == value->GetType());
      prefs_->Set(current->preference_path, value);
      return true;
    }
  }
  return false;
}

bool ConfigurationPolicyPrefStore::ApplyProxyPolicy(PolicyType policy,
                                                    Value* value) {
  bool result = false;
  bool warn_about_proxy_disable_config = false;
  bool warn_about_proxy_system_config = false;

  const PolicyToPreferenceMapEntry* match_entry =
      FindPolicyInMap(policy, proxy_policy_map_, arraysize(proxy_policy_map_));

  // When the first proxy-related policy is applied, ALL proxy-related
  // preferences that have been set by command-line switches, extensions,
  // user preferences or any other mechanism are overridden. Otherwise
  // it's possible for a user to interfere with proxy policy by setting
  // proxy-related command-line switches or set proxy-related prefs in an
  // extension that are related, but not identical, to the ones set through
  // policy.
  if (!lower_priority_proxy_settings_overridden_ &&
      (match_entry ||
       policy == ConfigurationPolicyPrefStore::kPolicyProxyServerMode)) {
    ProxyPreferenceSet proxy_preference_set;
    GetProxyPreferenceSet(&proxy_preference_set);
    for (ProxyPreferenceSet::const_iterator i = proxy_preference_set.begin();
         i != proxy_preference_set.end(); ++i) {
      prefs_->Set(*i, PrefStore::CreateUseDefaultSentinelValue());
    }
    lower_priority_proxy_settings_overridden_ = true;
  }

  // Translate the proxy policy into preferences.
  if (policy == ConfigurationPolicyStore::kPolicyProxyServerMode) {
    int int_value;
    bool proxy_auto_detect = false;
    if (value->GetAsInteger(&int_value)) {
      result = true;
      switch (int_value) {
        case ConfigurationPolicyStore::kPolicyNoProxyServerMode:
          if (!proxy_disabled_) {
            if (proxy_configuration_specified_)
              warn_about_proxy_disable_config = true;
            proxy_disabled_ = true;
          }
          break;
        case ConfigurationPolicyStore::kPolicyAutoDetectProxyMode:
          proxy_auto_detect = true;
          break;
        case ConfigurationPolicyStore::kPolicyManuallyConfiguredProxyMode:
          break;
        case ConfigurationPolicyStore::kPolicyUseSystemProxyMode:
          if (!use_system_proxy_) {
            if (proxy_configuration_specified_)
              warn_about_proxy_system_config = true;
            use_system_proxy_ = true;
          }
          break;
        default:
          // Not a valid policy, don't assume ownership of |value|
          result = false;
          break;
      }

      if (int_value != kPolicyUseSystemProxyMode) {
        prefs_->Set(prefs::kNoProxyServer,
                    Value::CreateBooleanValue(proxy_disabled_));
        prefs_->Set(prefs::kProxyAutoDetect,
                    Value::CreateBooleanValue(proxy_auto_detect));
      }
    }
  } else if (match_entry) {
    // Determine if the applied proxy policy settings conflict and issue
    // a corresponding warning if they do.
    if (!proxy_configuration_specified_) {
      if (proxy_disabled_)
        warn_about_proxy_disable_config = true;
      if (use_system_proxy_)
        warn_about_proxy_system_config = true;
      proxy_configuration_specified_ = true;
    }
    if (!use_system_proxy_ && !proxy_disabled_) {
      prefs_->Set(match_entry->preference_path, value);
      // The ownership of value has been passed on to |prefs_|,
      // don't clean it up later.
      value = NULL;
    }
    result = true;
  }

  if (warn_about_proxy_disable_config) {
    LOG(WARNING) << "A centrally-administered policy disables the use of"
                 << " a proxy but also specifies an explicit proxy"
                 << " configuration.";
  }

  if (warn_about_proxy_system_config) {
    LOG(WARNING) << "A centrally-administered policy dictates that the"
                 << " system proxy settings should be used but also specifies"
                 << " an explicit proxy configuration.";
  }

  // If the policy was a proxy policy, cleanup |value|.
  if (result && value)
    delete value;
  return result;
}

bool ConfigurationPolicyPrefStore::ApplySyncPolicy(PolicyType policy,
                                                   Value* value) {
  if (policy == ConfigurationPolicyStore::kPolicySyncDisabled) {
    bool disable_sync;
    if (value->GetAsBoolean(&disable_sync) && disable_sync)
      prefs_->Set(prefs::kSyncManaged, value);
    else
      delete value;
    return true;
  }
  return false;
}

bool ConfigurationPolicyPrefStore::ApplyAutoFillPolicy(PolicyType policy,
                                                       Value* value) {
  if (policy == ConfigurationPolicyStore::kPolicyAutoFillEnabled) {
    bool auto_fill_enabled;
    if (value->GetAsBoolean(&auto_fill_enabled) && !auto_fill_enabled)
      prefs_->Set(prefs::kAutoFillEnabled, Value::CreateBooleanValue(false));
    delete value;
    return true;
  }
  return false;
}

void ConfigurationPolicyPrefStore::EnsureStringPrefExists(
    const std::string& path) {
  std::string value;
  if (!prefs_->GetString(path, &value))
    prefs_->SetString(path, value);
}

// Implementation of SearchTermsData just for validation.
class SearchTermsDataForValidation : public SearchTermsData {
 public:
  SearchTermsDataForValidation() {}

  // Implementation of SearchTermsData.
  virtual std::string GoogleBaseURLValue() const {
    return "http://www.google.com/";
  }
  virtual std::string GetApplicationLocale() const {
    return "en";
  }
#if defined(OS_WIN) && defined(GOOGLE_CHROME_BUILD)
  virtual std::wstring GetRlzParameterValue() const {
    return L"";
  }
#endif
 private:
  DISALLOW_COPY_AND_ASSIGN(SearchTermsDataForValidation);
};


void ConfigurationPolicyPrefStore::FinalizeDefaultSearchPolicySettings() {
  bool enabled = true;
  if (prefs_->GetBoolean(prefs::kDefaultSearchProviderEnabled, &enabled) &&
      !enabled) {
    // If default search is disabled, we ignore the other fields.
    prefs_->SetString(prefs::kDefaultSearchProviderName, "");
    prefs_->SetString(prefs::kDefaultSearchProviderSearchURL, "");
    prefs_->SetString(prefs::kDefaultSearchProviderSuggestURL, "");
    prefs_->SetString(prefs::kDefaultSearchProviderIconURL, "");
    prefs_->SetString(prefs::kDefaultSearchProviderEncodings, "");
    prefs_->SetString(prefs::kDefaultSearchProviderKeyword, "");
    return;
  }
  std::string search_url;
  // The search URL is required.
  if (prefs_->GetString(prefs::kDefaultSearchProviderSearchURL, &search_url) &&
      !search_url.empty()) {
    SearchTermsDataForValidation search_terms_data;
    TemplateURLRef search_url_ref(search_url, 0, 0);
    // It must support replacement (which implies it is valid).
    if (search_url_ref.SupportsReplacementUsingTermsData(search_terms_data)) {
      // The other entries are optional.  Just make sure that they are all
      // specified via policy, so that we don't use regular prefs.
      EnsureStringPrefExists(prefs::kDefaultSearchProviderSuggestURL);
      EnsureStringPrefExists(prefs::kDefaultSearchProviderIconURL);
      EnsureStringPrefExists(prefs::kDefaultSearchProviderEncodings);
      EnsureStringPrefExists(prefs::kDefaultSearchProviderKeyword);

      // For the name, default to the host if not specified.
      std::string name;
      if (!prefs_->GetString(prefs::kDefaultSearchProviderName, &name) ||
          name.empty())
        prefs_->SetString(prefs::kDefaultSearchProviderName,
                          GURL(search_url).host());

      // And clear the IDs since these are not specified via policy.
      prefs_->SetString(prefs::kDefaultSearchProviderID, "");
      prefs_->SetString(prefs::kDefaultSearchProviderPrepopulateID, "");
      return;
    }
  }
  // Required entries are not there.  Remove any related entries.
  RemovePreferencesOfMap(default_search_policy_map_,
                         arraysize(default_search_policy_map_));
}

}  // namespace policy
