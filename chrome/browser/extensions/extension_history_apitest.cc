// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/common/chrome_switches.h"
#include "net/base/mock_host_resolver.h"

// Flaky, http://crbug.com/26296.
IN_PROC_BROWSER_TEST_F(ExtensionApiTest, FLAKY_History) {
  host_resolver()->AddRule("www.a.com", "127.0.0.1");
  host_resolver()->AddRule("www.b.com", "127.0.0.1");
  ASSERT_TRUE(StartHTTPServer());

  ASSERT_TRUE(RunExtensionTest("history")) << message_;
}
