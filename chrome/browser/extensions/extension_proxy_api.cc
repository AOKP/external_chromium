// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_proxy_api.h"

#include "base/string_util.h"
#include "base/stringprintf.h"
#include "base/values.h"
#include "chrome/browser/extensions/extension_pref_store.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/pref_names.h"

namespace {

// The scheme for which to use a manually specified proxy, not of the proxy URI
// itself.
enum {
  SCHEME_ALL = 0,
  SCHEME_HTTP,
  SCHEME_HTTPS,
  SCHEME_FTP,
  SCHEME_SOCKS,
  SCHEME_MAX = SCHEME_SOCKS  // Keep this value up to date.
};

// The names of the JavaScript properties to extract from the proxy_rules.
// These must be kept in sync with the SCHEME_* constants.
const char* field_name[] = { "singleProxy",
                             "proxyForHttp",
                             "proxyForHttps",
                             "proxyForFtp",
                             "socksProxy" };

// The names of the schemes to be used to build the preference value string
// for manual proxy settings.  These must be kept in sync with the SCHEME_*
// constants.
const char* scheme_name[] = { "*error*",
                              "http",
                              "https",
                              "ftp",
                              "socks" };

}  // namespace

COMPILE_ASSERT(SCHEME_MAX == SCHEME_SOCKS, SCHEME_MAX_must_equal_SCHEME_SOCKS);
COMPILE_ASSERT(arraysize(field_name) == SCHEME_MAX + 1,
               field_name_array_is_wrong_size);
COMPILE_ASSERT(arraysize(scheme_name) == SCHEME_MAX + 1,
               scheme_name_array_is_wrong_size);

bool UseCustomProxySettingsFunction::RunImpl() {
  DictionaryValue* proxy_config;
  EXTENSION_FUNCTION_VALIDATE(args_->GetDictionary(0, &proxy_config));

  bool auto_detect = false;
  proxy_config->GetBoolean("autoDetect", &auto_detect);

  DictionaryValue* pac_dict = NULL;
  proxy_config->GetDictionary("pacScript", &pac_dict);

  DictionaryValue* proxy_rules = NULL;
  proxy_config->GetDictionary("rules", &proxy_rules);

  return ApplyAutoDetect(auto_detect) &&
      ApplyPacScript(pac_dict) &&
      ApplyProxyRules(proxy_rules);
}

bool UseCustomProxySettingsFunction::GetProxyServer(
    const DictionaryValue* dict, ProxyServer* proxy_server) {
  dict->GetString("scheme", &proxy_server->scheme);
  EXTENSION_FUNCTION_VALIDATE(dict->GetString("host", &proxy_server->host));
  dict->GetInteger("port", &proxy_server->port);
  return true;
}

bool UseCustomProxySettingsFunction::ApplyAutoDetect(bool auto_detect) {
  // We take control of the auto-detect preference even if none was specified,
  // so that all proxy preferences are controlled by the same extension (if not
  // by a higher-priority source).
  SendNotification(prefs::kProxyAutoDetect,
                   Value::CreateBooleanValue(auto_detect));
  return true;
}

bool UseCustomProxySettingsFunction::ApplyPacScript(DictionaryValue* pac_dict) {
  std::string pac_url;
  if (pac_dict)
    pac_dict->GetString("url", &pac_url);

  // We take control of the PAC preference even if none was specified, so that
  // all proxy preferences are controlled by the same extension (if not by a
  // higher-priority source).
  SendNotification(prefs::kProxyPacUrl, Value::CreateStringValue(pac_url));
  return true;
}

bool UseCustomProxySettingsFunction::ApplyProxyRules(
    DictionaryValue* proxy_rules) {
  if (!proxy_rules)
    return true;

  // Local data into which the parameters will be parsed. has_proxy describes
  // whether a setting was found for the scheme; proxy_dict holds the
  // DictionaryValues which in turn contain proxy server descriptions, and
  // proxy_server holds ProxyServer structs containing those descriptions.
  bool has_proxy[SCHEME_MAX + 1];
  DictionaryValue* proxy_dict[SCHEME_MAX + 1];
  ProxyServer proxy_server[SCHEME_MAX + 1];

  // Looking for all possible proxy types is inefficient if we have a
  // singleProxy that will supersede per-URL proxies, but it's worth it to keep
  // the code simple and extensible.
  for (size_t i = 0; i <= SCHEME_MAX; ++i) {
    has_proxy[i] = proxy_rules->GetDictionary(field_name[i], &proxy_dict[i]);
    if (has_proxy[i]) {
      if (!GetProxyServer(proxy_dict[i], &proxy_server[i]))
        return false;
    }
  }

  // A single proxy supersedes individual HTTP, HTTPS, and FTP proxies.
  if (has_proxy[SCHEME_ALL]) {
    proxy_server[SCHEME_HTTP] = proxy_server[SCHEME_ALL];
    proxy_server[SCHEME_HTTPS] = proxy_server[SCHEME_ALL];
    proxy_server[SCHEME_FTP] = proxy_server[SCHEME_ALL];
    has_proxy[SCHEME_HTTP] = true;
    has_proxy[SCHEME_HTTPS] = true;
    has_proxy[SCHEME_FTP] = true;
    has_proxy[SCHEME_ALL] = false;
  }

  // TODO(pamg): Ensure that if a value is empty, that means "don't use a proxy
  // for this scheme".

  // Build the proxy preference string.
  std::string proxy_pref;
  for (size_t i = 0; i <= SCHEME_MAX; ++i) {
    if (has_proxy[i]) {
      // http=foopy:4010;ftp=socks://foopy2:80
      if (!proxy_pref.empty())
        proxy_pref.append(";");
      proxy_pref.append(scheme_name[i]);
      proxy_pref.append("=");
      proxy_pref.append(proxy_server[i].scheme);
      proxy_pref.append("://");
      proxy_pref.append(proxy_server[i].host);
      if (proxy_server[i].port != ProxyServer::INVALID_PORT) {
        proxy_pref.append(":");
        proxy_pref.append(base::StringPrintf("%d", proxy_server[i].port));
      }
    }
  }

  SendNotification(prefs::kProxyServer, Value::CreateStringValue(proxy_pref));
  return true;
}

void UseCustomProxySettingsFunction::SendNotification(const char* pref_path,
                                                      Value* pref_value) {
  ExtensionPrefStore::ExtensionPrefDetails details =
      std::make_pair(GetExtension(), std::make_pair(pref_path, pref_value));

  NotificationService::current()->Notify(
      NotificationType::EXTENSION_PREF_CHANGED,
      Source<Profile>(profile_),
      Details<ExtensionPrefStore::ExtensionPrefDetails>(&details));
}
