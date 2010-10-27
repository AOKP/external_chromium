// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "chrome/browser/browser.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/extensions/extensions_service.h"
#include "chrome/browser/profile.h"
#include "chrome/common/chrome_switches.h"

class ExtensionManagementApiTest : public ExtensionApiTest {
 public:

  virtual void SetUpCommandLine(CommandLine* command_line) {
    ExtensionApiTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch(
        switches::kEnableExperimentalExtensionApis);
  }

  virtual void InstallExtensions() {
    FilePath basedir = test_data_dir_.AppendASCII("management");

    // Load 2 enabled items.
    ASSERT_TRUE(LoadExtension(basedir.AppendASCII("enabled_extension")));
    ASSERT_TRUE(LoadExtension(basedir.AppendASCII("enabled_app")));

    // Load 2 disabled items.
    ExtensionsService* service = browser()->profile()->GetExtensionsService();
    ASSERT_TRUE(LoadExtension(basedir.AppendASCII("disabled_extension")));
    service->DisableExtension(last_loaded_extension_id_);
    ASSERT_TRUE(LoadExtension(basedir.AppendASCII("disabled_app")));
    service->DisableExtension(last_loaded_extension_id_);
  }
};

IN_PROC_BROWSER_TEST_F(ExtensionManagementApiTest, Basics) {
  InstallExtensions();
  ASSERT_TRUE(RunExtensionSubtest("management/test", "basics.html"));
}

IN_PROC_BROWSER_TEST_F(ExtensionManagementApiTest, Uninstall) {
  InstallExtensions();
  ASSERT_TRUE(RunExtensionSubtest("management/test", "uninstall.html"));
}
