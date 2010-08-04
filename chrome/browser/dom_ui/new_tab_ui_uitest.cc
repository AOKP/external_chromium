// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/ui/ui_test.h"

#include "base/file_path.h"
#include "chrome/app/chrome_dll_resource.h"
#include "chrome/browser/chrome_thread.h"
#include "chrome/browser/dom_ui/new_tab_ui.h"
#include "chrome/browser/pref_value_store.h"
#include "chrome/common/json_pref_store.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/automation/browser_proxy.h"
#include "chrome/test/automation/tab_proxy.h"
#include "chrome/test/automation/window_proxy.h"
#include "chrome/test/testing_pref_service.h"

class NewTabUITest : public UITest {
 public:
  NewTabUITest() {
    dom_automation_enabled_ = true;
    // Set home page to the empty string so that we can set the home page using
    // preferences.
    homepage_ = L"";

    // Setup the DEFAULT_THEME profile (has fake history entries).
    set_template_user_data(UITest::ComputeTypicalUserDataSource(
        UITest::DEFAULT_THEME));
  }
};

TEST_F(NewTabUITest, NTPHasThumbnails) {
  // Switch to the "new tab" tab, which should be any new tab after the
  // first (the first is about:blank).
  scoped_refptr<BrowserProxy> window(automation()->GetBrowserWindow(0));
  ASSERT_TRUE(window.get());

  // Bring up a new tab page.
  ASSERT_TRUE(window->RunCommand(IDC_NEW_TAB));
  int load_time;
  ASSERT_TRUE(automation()->WaitForInitialNewTabUILoad(&load_time));

  // Blank thumbnails on the NTP have the class 'filler' applied to the div.
  // If all the thumbnails load, there should be no div's with 'filler'.
  scoped_refptr<TabProxy> tab = window->GetActiveTab();
  ASSERT_TRUE(tab.get());

  ASSERT_TRUE(WaitUntilJavaScriptCondition(tab, L"",
      L"window.domAutomationController.send("
      L"document.getElementsByClassName('filler').length == 0)",
      action_max_timeout_ms()));
}

// Fails about ~5% of the time on all platforms. http://crbug.com/45001
TEST_F(NewTabUITest, FLAKY_ChromeInternalLoadsNTP) {
  scoped_refptr<BrowserProxy> window(automation()->GetBrowserWindow(0));
  ASSERT_TRUE(window.get());

  // Go to the "new tab page" using its old url, rather than chrome://newtab.
  scoped_refptr<TabProxy> tab = window->GetTab(0);
  ASSERT_TRUE(tab.get());
  ASSERT_TRUE(tab->NavigateToURLAsync(GURL("chrome-internal:")));
  int load_time;
  ASSERT_TRUE(automation()->WaitForInitialNewTabUILoad(&load_time));

  // Ensure there are some thumbnails loaded in the page.
  int thumbnails_count = -1;
  ASSERT_TRUE(tab->ExecuteAndExtractInt(L"",
      L"window.domAutomationController.send("
      L"document.getElementsByClassName('thumbnail-container').length)",
      &thumbnails_count));
  EXPECT_GT(thumbnails_count, 0);
}

TEST_F(NewTabUITest, UpdateUserPrefsVersion) {
  // PrefService with JSON user-pref file only, no enforced or advised prefs.
  scoped_ptr<PrefService> prefs(new TestingPrefService);

  // Does the migration
  NewTabUI::RegisterUserPrefs(prefs.get());

  ASSERT_EQ(NewTabUI::current_pref_version(),
            prefs->GetInteger(prefs::kNTPPrefVersion));

  // Reset the version
  prefs->ClearPref(prefs::kNTPPrefVersion);
  ASSERT_EQ(0, prefs->GetInteger(prefs::kNTPPrefVersion));

  bool migrated = NewTabUI::UpdateUserPrefsVersion(prefs.get());
  ASSERT_TRUE(migrated);
  ASSERT_EQ(NewTabUI::current_pref_version(),
            prefs->GetInteger(prefs::kNTPPrefVersion));

  migrated = NewTabUI::UpdateUserPrefsVersion(prefs.get());
  ASSERT_FALSE(migrated);
}

TEST_F(NewTabUITest, HomePageLink) {
  scoped_refptr<BrowserProxy> browser(automation()->GetBrowserWindow(0));
  ASSERT_TRUE(browser.get());

  ASSERT_TRUE(
      browser->SetBooleanPreference(prefs::kHomePageIsNewTabPage, false));

  // Bring up a new tab page.
  ASSERT_TRUE(browser->RunCommand(IDC_NEW_TAB));
  int load_time;
  ASSERT_TRUE(automation()->WaitForInitialNewTabUILoad(&load_time));

  scoped_refptr<TabProxy> tab = browser->GetActiveTab();
  ASSERT_TRUE(tab.get());

  // TODO(arv): Extract common patterns for doing js testing.

  // Fire click. Because tip service is turned off for testing, we first
  // force the "make this my home page" tip to appear.
  // TODO(arv): Find screen position of element and use a lower level click
  // emulation.
  bool result;
  ASSERT_TRUE(tab->ExecuteAndExtractBool(L"",
    L"window.domAutomationController.send("
    L"(function() {"
    L"  tipCache = [{\"set_homepage_tip\":\"Make this the home page\"}];"
    L"  renderTip();"
    L"  var e = document.createEvent('Event');"
    L"  e.initEvent('click', true, true);"
    L"  var el = document.querySelector('#tip-line > button');"
    L"  el.dispatchEvent(e);"
    L"  return true;"
    L"})()"
    L")",
    &result));
  ASSERT_TRUE(result);

  // Make sure text of "set as home page" tip has been removed.
  std::wstring tip_text_content;
  ASSERT_TRUE(tab->ExecuteAndExtractString(L"",
    L"window.domAutomationController.send("
    L"(function() {"
    L"  var el = document.querySelector('#tip-line');"
    L"  return el.textContent;"
    L"})()"
    L")",
    &tip_text_content));
  ASSERT_EQ(L"", tip_text_content);

  // Make sure that the notification is visible
  bool has_class;
  ASSERT_TRUE(tab->ExecuteAndExtractBool(L"",
    L"window.domAutomationController.send("
    L"(function() {"
    L"  var el = document.querySelector('#notification');"
    L"  return el.classList.contains('show');"
    L"})()"
    L")",
    &has_class));
  ASSERT_TRUE(has_class);

  bool is_home_page;
  ASSERT_TRUE(browser->GetBooleanPreference(prefs::kHomePageIsNewTabPage,
                                            &is_home_page));
  ASSERT_TRUE(is_home_page);
}
