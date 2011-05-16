// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_POLICY_CONFIGURATION_POLICY_STORE_INTERFACE_H_
#define CHROME_BROWSER_POLICY_CONFIGURATION_POLICY_STORE_INTERFACE_H_
#pragma once

#include "base/basictypes.h"

class Value;

namespace policy {

enum ConfigurationPolicyType {
  kPolicyHomePage,
  kPolicyHomepageIsNewTabPage,
  kPolicyRestoreOnStartup,
  kPolicyURLsToRestoreOnStartup,
  kPolicyDefaultSearchProviderEnabled,
  kPolicyDefaultSearchProviderName,
  kPolicyDefaultSearchProviderKeyword,
  kPolicyDefaultSearchProviderSearchURL,
  kPolicyDefaultSearchProviderSuggestURL,
  kPolicyDefaultSearchProviderIconURL,
  kPolicyDefaultSearchProviderEncodings,
  kPolicyDisableSpdy,
  kPolicyProxyMode,
  kPolicyProxyServer,
  kPolicyProxyPacUrl,
  kPolicyProxyBypassList,
  kPolicyAlternateErrorPagesEnabled,
  kPolicySearchSuggestEnabled,
  kPolicyDnsPrefetchingEnabled,
  kPolicySafeBrowsingEnabled,
  kPolicyMetricsReportingEnabled,
  kPolicyPasswordManagerEnabled,
  kPolicyPasswordManagerAllowShowPasswords,
  kPolicyAutoFillEnabled,
  kPolicySyncDisabled,
  kPolicyApplicationLocale,
  kPolicyExtensionInstallAllowList,
  kPolicyExtensionInstallDenyList,
  kPolicyShowHomeButton,
  kPolicyDisabledPlugins,
  kPolicyPrintingEnabled,
  kPolicyChromeFrameRendererSettings,
  kPolicyRenderInChromeFrameList,
  kPolicyRenderInHostList,
  kPolicyJavascriptEnabled,
  kPolicySavingBrowserHistoryDisabled,
  kPolicyDeveloperToolsDisabled,
  kPolicyBlockThirdPartyCookies,
  kPolicyDefaultCookiesSetting,
  kPolicyDefaultImagesSetting,
  kPolicyDefaultJavaScriptSetting,
  kPolicyDefaultPluginsSetting,
  kPolicyDefaultPopupsSetting,
  kPolicyDefaultNotificationSetting,
  kPolicyDefaultGeolocationSetting,
  kPolicyExtensionInstallForceList,
  kPolicyChromeOsLockOnIdleSuspend,
  kPolicyAuthSchemes,
  kPolicyDisableAuthNegotiateCnameLookup,
  kPolicyEnableAuthNegotiatePort,
  kPolicyAuthServerWhitelist,
  kPolicyAuthNegotiateDelegateWhitelist,
  kPolicyGSSAPILibraryName,
  kPolicyDisable3DAPIs
};


// Constants for the "Proxy Server Mode" defined in the policies.
// Note that these diverge from internal presentation defined in
// ProxyPrefs::ProxyMode for legacy reasons. The following four
// PolicyProxyModeType types were not very precise and had overlapping use
// cases.
enum PolicyProxyModeType {
  // Disable Proxy, connect directly.
  kPolicyNoProxyServerMode = 0,
  // Auto detect proxy or use specific PAC script if given.
  kPolicyAutoDetectProxyMode = 1,
  // Use manually configured proxy servers (fixed servers).
  kPolicyManuallyConfiguredProxyMode = 2,
  // Use system proxy server.
  kPolicyUseSystemProxyMode = 3,

  MODE_COUNT
};

// An abstract super class for policy stores that provides a method that can be
// called by a |ConfigurationPolicyProvider| to specify a policy.
class ConfigurationPolicyStoreInterface {
 public:
  virtual ~ConfigurationPolicyStoreInterface() {}

  // A |ConfigurationPolicyProvider| specifies the value of a policy
  // setting through a call to |Apply|.  The configuration policy pref
  // store takes over the ownership of |value|.
  virtual void Apply(ConfigurationPolicyType policy, Value* value) = 0;

 protected:
  ConfigurationPolicyStoreInterface() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(ConfigurationPolicyStoreInterface);
};

}  // namespace policy

#endif  // CHROME_BROWSER_POLICY_CONFIGURATION_POLICY_STORE_INTERFACE_H_
