// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CONFIGURATION_POLICY_PROVIDER_H_
#define CHROME_BROWSER_CONFIGURATION_POLICY_PROVIDER_H_

#include <vector>

#include "base/basictypes.h"
#include "chrome/browser/configuration_policy_store.h"

class DictionaryValue;

// A mostly-abstract super class for platform-specific policy providers.
// Platform-specific policy providers (Windows Group Policy, gconf,
// etc.) should implement a subclass of this class.
class ConfigurationPolicyProvider {
 public:
  ConfigurationPolicyProvider() {}
  virtual ~ConfigurationPolicyProvider() {}

  // Must be implemented by provider subclasses to specify the
  // provider-specific policy decisions. The preference service
  // invokes this |Provide| method when it needs a policy
  // provider to specify its policy choices. In |Provide|,
  // the |ConfigurationPolicyProvider| must make calls to the
  // |Apply| method of |store| to apply specific policies.
  // Returns true if the policy could be provided, otherwise false.
  virtual bool Provide(ConfigurationPolicyStore* store) = 0;

 protected:
  // A structure mapping policies to their implementations by providers.
  struct PolicyValueMapEntry {
    ConfigurationPolicyStore::PolicyType policy_type;
    Value::ValueType value_type;
    std::string name;
  };
  typedef std::vector<PolicyValueMapEntry> PolicyValueMap;

  // Returns the mapping from policy values to the actual names used by
  // implementations.
  static const PolicyValueMap* PolicyValueMapping();

 private:
  DISALLOW_COPY_AND_ASSIGN(ConfigurationPolicyProvider);
};

#endif  // CHROME_BROWSER_CONFIGURATION_POLICY_PROVIDER_H_

