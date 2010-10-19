// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/chrome_switches.h"
#include "chrome/test/automation/browser_proxy.h"
#include "chrome/test/automation/tab_proxy.h"
#include "chrome/test/ui/ui_test.h"
#include "net/base/net_util.h"
#include "net/test/test_server.h"

class NotificationsPermissionTest : public UITest {
 public:
  NotificationsPermissionTest() {
    dom_automation_enabled_ = true;
    show_window_ = true;
  }
};

TEST_F(NotificationsPermissionTest, TestUserGestureInfobar) {
  net::TestServer test_server(net::TestServer::TYPE_HTTP,
                              FilePath(FILE_PATH_LITERAL("chrome/test/data")));
  ASSERT_TRUE(test_server.Start());

  scoped_refptr<BrowserProxy> browser(automation()->GetBrowserWindow(0));
  ASSERT_TRUE(browser.get());
  scoped_refptr<TabProxy> tab(browser->GetActiveTab());
  ASSERT_TRUE(tab.get());
  ASSERT_EQ(AUTOMATION_MSG_NAVIGATION_SUCCESS,
            tab->NavigateToURL(test_server.GetURL(
                "files/notifications/notifications_request_function.html")));
  WaitUntilTabCount(1);

  // Request permission by calling request() while eval'ing an inline script;
  // That's considered a user gesture to webkit, and should produce an infobar.
  bool result;
  ASSERT_TRUE(tab->ExecuteAndExtractBool(
      L"",
      L"window.domAutomationController.send(request());",
      &result));
  EXPECT_TRUE(result);

  EXPECT_TRUE(tab->WaitForInfoBarCount(1));
}

TEST_F(NotificationsPermissionTest, TestNoUserGestureInfobar) {
  net::TestServer test_server(net::TestServer::TYPE_HTTP,
                              FilePath(FILE_PATH_LITERAL("chrome/test/data")));
  ASSERT_TRUE(test_server.Start());

  scoped_refptr<BrowserProxy> browser(automation()->GetBrowserWindow(0));
  ASSERT_TRUE(browser.get());
  scoped_refptr<TabProxy> tab(browser->GetActiveTab());
  ASSERT_TRUE(tab.get());

  // Load a page which just does a request; no user gesture should result
  // in no infobar.
  ASSERT_EQ(AUTOMATION_MSG_NAVIGATION_SUCCESS,
            tab->NavigateToURL(test_server.GetURL(
                "files/notifications/notifications_request_inline.html")));
  WaitUntilTabCount(1);

  int info_bar_count;
  ASSERT_TRUE(tab->GetInfoBarCount(&info_bar_count));
  EXPECT_EQ(0, info_bar_count);
}
