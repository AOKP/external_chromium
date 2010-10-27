// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_POLICY_DUMMY_CONFIGURATION_POLICY_PROVIDER_H_
#define CHROME_BROWSER_POLICY_DUMMY_CONFIGURATION_POLICY_PROVIDER_H_
#pragma once

#include "chrome/browser/policy/configuration_policy_store.h"
#include "chrome/browser/policy/configuration_policy_provider.h"

namespace policy {

class DummyConfigurationPolicyProvider : public ConfigurationPolicyProvider {
 public:
  explicit DummyConfigurationPolicyProvider(
      const StaticPolicyValueMap& policy_map)
      : ConfigurationPolicyProvider(policy_map) {
  }
  virtual ~DummyConfigurationPolicyProvider() {}

  virtual bool Provide(ConfigurationPolicyStore* store) {
    return true;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(DummyConfigurationPolicyProvider);
};

}  // namespace policy

#endif  // CHROME_BROWSER_POLICY_DUMMY_CONFIGURATION_POLICY_PROVIDER_H_
