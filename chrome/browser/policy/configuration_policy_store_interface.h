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
  kPolicyProxyServerMode,
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
  kPolicyExtensionInstallForceList,
  kPolicyChromeOsLockOnIdleSuspend,
  kPolicyAuthSchemes,
  kPolicyDisableAuthNegotiateCnameLookup,
  kPolicyEnableAuthNegotiatePort,
  kPolicyAuthServerWhitelist,
  kPolicyAuthNegotiateDelegateWhitelist,
  kPolicyGSSAPILibraryName,
};

static const int kPolicyNoProxyServerMode = 0;
static const int kPolicyAutoDetectProxyMode = 1;
static const int kPolicyManuallyConfiguredProxyMode = 2;
static const int kPolicyUseSystemProxyMode = 3;

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
