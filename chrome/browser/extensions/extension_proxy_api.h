// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_PROXY_API_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_PROXY_API_H_

#include <string>

#include "chrome/browser/extensions/extension_function.h"

class DictionaryValue;

class UseCustomProxySettingsFunction : public SyncExtensionFunction {
 public:
  ~UseCustomProxySettingsFunction() {}
  virtual bool RunImpl();

  DECLARE_EXTENSION_FUNCTION_NAME("experimental.proxy.useCustomProxySettings")

 private:
  struct ProxyServer {
    enum {
      INVALID_PORT = -1
    };
    ProxyServer() : scheme("http"), host(""), port(INVALID_PORT) {}

    // The scheme of the proxy URI itself.
    std::string scheme;
    std::string host;
    int port;
  };

  bool GetProxyServer(const DictionaryValue* dict, ProxyServer* proxy_server);

  bool ApplyAutoDetect(bool auto_detect);
  bool ApplyPacScript(DictionaryValue* pac_dict);
  bool ApplyProxyRules(DictionaryValue* proxy_rules);

  // Sends a notification that the given pref would like to change to the
  // indicated pref_value. This is mainly useful so the ExtensionPrefStore can
  // apply the requested change.
  void SendNotification(const char* pref_path, Value* pref_value);
};

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_PROXY_API_H_
