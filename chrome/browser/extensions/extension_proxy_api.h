// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_PROXY_API_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_PROXY_API_H_

#include <string>

#include "chrome/browser/extensions/extension_function.h"

namespace net {
class ProxyServer;
}

class DictionaryValue;

class ProxySettingsFunction : public SyncExtensionFunction {
 public:
  virtual ~ProxySettingsFunction() {}
  virtual bool RunImpl() = 0;
 protected:
  // Takes ownership of |pref_value|.
  void ApplyPreference(
      const char* pref_path, Value* pref_value, bool incognito);
  void RemovePreference(const char* pref_path, bool incognito);
};

class UseCustomProxySettingsFunction : public ProxySettingsFunction {
 public:
  virtual ~UseCustomProxySettingsFunction() {}
  virtual bool RunImpl();

  DECLARE_EXTENSION_FUNCTION_NAME("experimental.proxy.useCustomProxySettings")
};

class RemoveCustomProxySettingsFunction : public ProxySettingsFunction {
 public:
  virtual ~RemoveCustomProxySettingsFunction() {}
  virtual bool RunImpl();

  DECLARE_EXTENSION_FUNCTION_NAME(
      "experimental.proxy.removeCustomProxySettings")
};

class GetCurrentProxySettingsFunction : public ProxySettingsFunction {
 public:
  virtual ~GetCurrentProxySettingsFunction() {}
  virtual bool RunImpl();

  DECLARE_EXTENSION_FUNCTION_NAME(
      "experimental.proxy.getCurrentProxySettings")
 private:
  // Convert the representation of a proxy configuration from the format
  // that is stored in the pref stores to the format that is used by the API.
  // See ProxyServer type defined in |experimental.proxy|.
  bool ConvertToApiFormat(const DictionaryValue* proxy_prefs,
                          DictionaryValue* api_proxy_config) const;
  bool ParseRules(const std::string& rules, DictionaryValue* out) const;
  DictionaryValue* ConvertToDictionary(const net::ProxyServer& proxy) const;
};

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_PROXY_API_H_
