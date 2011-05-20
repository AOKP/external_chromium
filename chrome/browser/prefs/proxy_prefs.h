// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PREFS_PROXY_PREFS_H_
#define CHROME_BROWSER_PREFS_PROXY_PREFS_H_
#pragma once

#include <string>

namespace ProxyPrefs {

// Possible types of specifying proxy settings. Do not change the order of
// the constants, because numeric values are exposed to users.
// If you add an enum constant, you should also add a string to
// kProxyModeNames in the .cc file.
enum ProxyMode {
  // Direct connection to the network, other proxy preferences are ignored.
  MODE_DIRECT = 0,

  // Try to retrieve a PAC script from http://wpad/wpad.dat or fall back to
  // direct connection.
  MODE_AUTO_DETECT = 1,

  // Try to retrieve a PAC script from kProxyPacURL or fall back to direct
  // connection.
  MODE_PAC_SCRIPT = 2,

  // Use the settings specified in kProxyServer and kProxyBypassList.
  MODE_FIXED_SERVERS = 3,

  // The system's proxy settings are used, other proxy preferences are
  // ignored.
  MODE_SYSTEM = 4,

  kModeCount
};

bool IntToProxyMode(int in_value, ProxyMode* out_value);
bool StringToProxyMode(const std::string& in_value,
                       ProxyMode* out_value);

}  // namespace ProxyPrefs

#endif  // CHROME_BROWSER_PREFS_PROXY_PREFS_H_
