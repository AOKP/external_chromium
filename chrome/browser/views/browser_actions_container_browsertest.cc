// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/browser.h"
#include "chrome/browser/extensions/browser_action_test_util.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extensions_service.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/views/browser_actions_container.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension_action.h"
#include "chrome/common/extensions/extension_resource.h"

class BrowserActionsContainerTest : public ExtensionBrowserTest {
 public:
  BrowserActionsContainerTest() {
  }
  virtual ~BrowserActionsContainerTest() {}

  virtual Browser* CreateBrowser(Profile* profile) {
    Browser* b = InProcessBrowserTest::CreateBrowser(profile);
    browser_actions_bar_.reset(new BrowserActionTestUtil(b));
    return b;
  }

  BrowserActionTestUtil* browser_actions_bar() {
    return browser_actions_bar_.get();
  }

  // Make sure extension with index |extension_index| has an icon.
  void EnsureExtensionHasIcon(int extension_index) {
    if (!browser_actions_bar_->HasIcon(extension_index)) {
      // The icon is loaded asynchronously and a notification is then sent to
      // observers. So we wait on it.
      browser_actions_bar_->WaitForBrowserActionUpdated(extension_index);
    }
    EXPECT_TRUE(browser_actions_bar()->HasIcon(extension_index));
  }

 private:
  scoped_ptr<BrowserActionTestUtil> browser_actions_bar_;
};

// Test the basic functionality.
IN_PROC_BROWSER_TEST_F(BrowserActionsContainerTest, Basic) {
  BrowserActionsContainer::disable_animations_during_testing_ = true;

  // Load an extension with no browser action.
  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("api_test")
                                          .AppendASCII("browser_action")
                                          .AppendASCII("none")));
  // This extension should not be in the model (has no browser action).
  EXPECT_EQ(0, browser_actions_bar()->NumberOfBrowserActions());

  // Load an extension with a browser action.
  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("api_test")
                                          .AppendASCII("browser_action")
                                          .AppendASCII("basics")));
  EXPECT_EQ(1, browser_actions_bar()->NumberOfBrowserActions());
  EnsureExtensionHasIcon(0);

  // Unload the extension.
  std::string id = browser_actions_bar()->GetExtensionId(0);
  UnloadExtension(id);
  EXPECT_EQ(0, browser_actions_bar()->NumberOfBrowserActions());
}

// TODO(mpcomplete): http://code.google.com/p/chromium/issues/detail?id=38992
// Disabled, http://crbug.com/38992.
IN_PROC_BROWSER_TEST_F(BrowserActionsContainerTest, DISABLED_Visibility) {
  BrowserActionsContainer::disable_animations_during_testing_ = true;

  // Load extension A (contains browser action).
  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("api_test")
                                          .AppendASCII("browser_action")
                                          .AppendASCII("basics")));
  EXPECT_EQ(1, browser_actions_bar()->NumberOfBrowserActions());
  EnsureExtensionHasIcon(0);
  EXPECT_EQ(1, browser_actions_bar()->VisibleBrowserActions());
  std::string idA = browser_actions_bar()->GetExtensionId(0);

  // Load extension B (contains browser action).
  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("api_test")
                                          .AppendASCII("browser_action")
                                          .AppendASCII("add_popup")));
  EXPECT_EQ(2, browser_actions_bar()->NumberOfBrowserActions());
  EnsureExtensionHasIcon(0);
  EXPECT_EQ(2, browser_actions_bar()->VisibleBrowserActions());
  std::string idB = browser_actions_bar()->GetExtensionId(1);

  EXPECT_NE(idA, idB);

  // Load extension C (contains browser action).
  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("api_test")
                                          .AppendASCII("browser_action")
                                          .AppendASCII("remove_popup")));
  EXPECT_EQ(3, browser_actions_bar()->NumberOfBrowserActions());
  EnsureExtensionHasIcon(2);
  EXPECT_EQ(3, browser_actions_bar()->VisibleBrowserActions());
  std::string idC = browser_actions_bar()->GetExtensionId(2);

  // Change container to show only one action, rest in overflow: A, [B, C].
  browser_actions_bar()->SetIconVisibilityCount(1);
  EXPECT_EQ(1, browser_actions_bar()->VisibleBrowserActions());

  // Disable extension A (should disappear). State becomes: B [C].
  DisableExtension(idA);
  EXPECT_EQ(2, browser_actions_bar()->NumberOfBrowserActions());
  EXPECT_EQ(1, browser_actions_bar()->VisibleBrowserActions());
  EXPECT_EQ(idB, browser_actions_bar()->GetExtensionId(0));

  // Enable A again. A should get its spot in the same location and the bar
  // should not grow (chevron is showing). For details: http://crbug.com/35349.
  // State becomes: A, [B, C].
  EnableExtension(idA);
  EXPECT_EQ(3, browser_actions_bar()->NumberOfBrowserActions());
  EXPECT_EQ(1, browser_actions_bar()->VisibleBrowserActions());
  EXPECT_EQ(idA, browser_actions_bar()->GetExtensionId(0));

  // Disable C (in overflow). State becomes: A, [B].
  DisableExtension(idC);
  EXPECT_EQ(2, browser_actions_bar()->NumberOfBrowserActions());
  EXPECT_EQ(1, browser_actions_bar()->VisibleBrowserActions());
  EXPECT_EQ(idA, browser_actions_bar()->GetExtensionId(0));

  // Enable C again. State becomes: A, [B, C].
  EnableExtension(idC);
  EXPECT_EQ(3, browser_actions_bar()->NumberOfBrowserActions());
  EXPECT_EQ(1, browser_actions_bar()->VisibleBrowserActions());
  EXPECT_EQ(idA, browser_actions_bar()->GetExtensionId(0));

  // Now we have 3 extensions. Make sure they are all visible. State: A, B, C.
  browser_actions_bar()->SetIconVisibilityCount(3);
  EXPECT_EQ(3, browser_actions_bar()->VisibleBrowserActions());

  // Disable extension A (should disappear). State becomes: B, C.
  DisableExtension(idA);
  EXPECT_EQ(2, browser_actions_bar()->NumberOfBrowserActions());
  EXPECT_EQ(2, browser_actions_bar()->VisibleBrowserActions());
  EXPECT_EQ(idB, browser_actions_bar()->GetExtensionId(0));

  // Disable extension B (should disappear). State becomes: C.
  DisableExtension(idB);
  EXPECT_EQ(1, browser_actions_bar()->NumberOfBrowserActions());
  EXPECT_EQ(1, browser_actions_bar()->VisibleBrowserActions());
  EXPECT_EQ(idC, browser_actions_bar()->GetExtensionId(0));

  // Enable B (makes B and C showing now). State becomes: B, C.
  EnableExtension(idB);
  EXPECT_EQ(2, browser_actions_bar()->NumberOfBrowserActions());
  EXPECT_EQ(2, browser_actions_bar()->VisibleBrowserActions());
  EXPECT_EQ(idB, browser_actions_bar()->GetExtensionId(0));

  // Enable A (makes A, B and C showing now). State becomes: B, C, A.
  EnableExtension(idA);
  EXPECT_EQ(3, browser_actions_bar()->NumberOfBrowserActions());
  EXPECT_EQ(3, browser_actions_bar()->VisibleBrowserActions());
  EXPECT_EQ(idA, browser_actions_bar()->GetExtensionId(2));
}

IN_PROC_BROWSER_TEST_F(BrowserActionsContainerTest, TestCrash57536) {
  std::cout << "Test starting\n" << std::flush;

  ExtensionsService* service = browser()->profile()->GetExtensionsService();
  const size_t size_before = service->extensions()->size();

  std::cout << "Loading extension\n" << std::flush;

  // Load extension A (contains browser action).
  ASSERT_TRUE(LoadExtension(test_data_dir_.AppendASCII("api_test")
                                          .AppendASCII("browser_action")
                                          .AppendASCII("crash_57536")));

  const Extension* extension = service->extensions()->at(size_before);

  std::cout << "Creating bitmap\n" << std::flush;

  // Create and cache and empty bitmap.
  SkBitmap bitmap;
  bitmap.setConfig(SkBitmap::kARGB_8888_Config,
                    Extension::kBrowserActionIconMaxSize,
                    Extension::kBrowserActionIconMaxSize);
  bitmap.allocPixels();

  std::cout << "Set as cached image\n" << std::flush;

  gfx::Size size(Extension::kBrowserActionIconMaxSize,
                 Extension::kBrowserActionIconMaxSize);
  extension->SetCachedImage(
      extension->GetResource(extension->browser_action()->default_icon_path()),
      bitmap,
      size);

  std::cout << "Disabling extension\n" << std::flush;
  DisableExtension(extension->id());
  std::cout << "Enabling extension\n" << std::flush;
  EnableExtension(extension->id());
  std::cout << "Test ending\n" << std::flush;
}
