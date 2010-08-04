// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "chrome/browser/net/url_fixer_upper.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/automation/tab_proxy.h"
#include "chrome/test/automation/browser_proxy.h"
#include "chrome/test/ui/ui_test.h"
#include "net/url_request/url_request_unittest.h"

using std::wstring;

namespace {

const wchar_t kDocRoot[] = L"chrome/test/data";

}  // namespace

class LoginPromptTest : public UITest {
 protected:
  LoginPromptTest()
      : UITest(),
        username_basic_(L"basicuser"),
        username_digest_(L"digestuser"),
        password_(L"secret"),
        password_bad_(L"denyme") {
  }

  void AppendTab(const GURL& url) {
    scoped_refptr<BrowserProxy> window_proxy(automation()->GetBrowserWindow(0));
    ASSERT_TRUE(window_proxy.get());
    ASSERT_TRUE(window_proxy->AppendTab(url));
  }

 protected:
  wstring username_basic_;
  wstring username_digest_;
  wstring password_;
  wstring password_bad_;
};

wstring ExpectedTitleFromAuth(const wstring& username,
                              const wstring& password) {
  // The TestServer sets the title to username/password on successful login.
  return username + L"/" + password;
}

// Test that "Basic" HTTP authentication works.
TEST_F(LoginPromptTest, TestBasicAuth) {
  scoped_refptr<HTTPTestServer> server =
      HTTPTestServer::CreateServer(kDocRoot, NULL);
  ASSERT_TRUE(NULL != server.get());
  scoped_refptr<TabProxy> tab(GetActiveTab());
  ASSERT_TRUE(tab.get());
  ASSERT_TRUE(tab->NavigateToURL(server->TestServerPage("auth-basic")));

  EXPECT_TRUE(tab->NeedsAuth());
  EXPECT_FALSE(tab->SetAuth(username_basic_, password_bad_));
  EXPECT_TRUE(tab->NeedsAuth());
  EXPECT_TRUE(tab->CancelAuth());
  EXPECT_EQ(L"Denied: wrong password", GetActiveTabTitle());

  ASSERT_TRUE(tab->NavigateToURL(server->TestServerPage("auth-basic")));

  EXPECT_TRUE(tab->NeedsAuth());
  EXPECT_TRUE(tab->SetAuth(username_basic_, password_));
  EXPECT_EQ(ExpectedTitleFromAuth(username_basic_, password_),
            GetActiveTabTitle());
}

// Test that "Digest" HTTP authentication works.
TEST_F(LoginPromptTest, TestDigestAuth) {
  scoped_refptr<HTTPTestServer> server =
      HTTPTestServer::CreateServer(kDocRoot, NULL);
  ASSERT_TRUE(NULL != server.get());
  scoped_refptr<TabProxy> tab(GetActiveTab());
  ASSERT_TRUE(tab.get());
  ASSERT_TRUE(tab->NavigateToURL(server->TestServerPage("auth-digest")));

  EXPECT_TRUE(tab->NeedsAuth());
  EXPECT_FALSE(tab->SetAuth(username_digest_, password_bad_));
  EXPECT_TRUE(tab->CancelAuth());
  EXPECT_EQ(L"Denied: wrong password", GetActiveTabTitle());

  ASSERT_TRUE(tab->NavigateToURL(server->TestServerPage("auth-digest")));

  EXPECT_TRUE(tab->NeedsAuth());
  EXPECT_TRUE(tab->SetAuth(username_digest_, password_));
  EXPECT_EQ(ExpectedTitleFromAuth(username_digest_, password_),
            GetActiveTabTitle());
}

// Test that logging in on 2 tabs at once works.
TEST_F(LoginPromptTest, TestTwoAuths) {
  scoped_refptr<HTTPTestServer> server =
      HTTPTestServer::CreateServer(kDocRoot, NULL);
  ASSERT_TRUE(NULL != server.get());

  scoped_refptr<TabProxy> basic_tab(GetActiveTab());
  ASSERT_TRUE(basic_tab.get());
  ASSERT_TRUE(basic_tab->NavigateToURL(server->TestServerPage("auth-basic")));

  AppendTab(GURL(chrome::kAboutBlankURL));
  scoped_refptr<TabProxy> digest_tab(GetActiveTab());
  ASSERT_TRUE(digest_tab.get());
  ASSERT_TRUE(
      digest_tab->NavigateToURL(server->TestServerPage("auth-digest")));

  EXPECT_TRUE(basic_tab->NeedsAuth());
  EXPECT_TRUE(basic_tab->SetAuth(username_basic_, password_));
  EXPECT_TRUE(digest_tab->NeedsAuth());
  EXPECT_TRUE(digest_tab->SetAuth(username_digest_, password_));

  wstring title;
  EXPECT_TRUE(basic_tab->GetTabTitle(&title));
  EXPECT_EQ(ExpectedTitleFromAuth(username_basic_, password_), title);

  EXPECT_TRUE(digest_tab->GetTabTitle(&title));
  EXPECT_EQ(ExpectedTitleFromAuth(username_digest_, password_), title);
}

// Test that cancelling authentication works.
TEST_F(LoginPromptTest, TestCancelAuth) {
  scoped_refptr<HTTPTestServer> server =
      HTTPTestServer::CreateServer(kDocRoot, NULL);
  ASSERT_TRUE(NULL != server.get());
  scoped_refptr<TabProxy> tab(GetActiveTab());
  ASSERT_TRUE(tab.get());

  // First navigate to a test server page so we have something to go back to.
  ASSERT_TRUE(tab->NavigateToURL(server->TestServerPage("a")));

  // Navigating while auth is requested is the same as cancelling.
  ASSERT_TRUE(tab->NavigateToURL(server->TestServerPage("auth-basic")));
  EXPECT_TRUE(tab->NeedsAuth());
  ASSERT_TRUE(tab->NavigateToURL(server->TestServerPage("b")));
  EXPECT_FALSE(tab->NeedsAuth());

  ASSERT_TRUE(tab->NavigateToURL(server->TestServerPage("auth-basic")));
  EXPECT_TRUE(tab->NeedsAuth());
  EXPECT_TRUE(tab->GoBack());  // should bring us back to 'a'
  EXPECT_FALSE(tab->NeedsAuth());

  // Now add a page and go back, so we have something to go forward to.
  ASSERT_TRUE(tab->NavigateToURL(server->TestServerPage("c")));
  EXPECT_TRUE(tab->GoBack());  // should bring us back to 'a'

  ASSERT_TRUE(tab->NavigateToURL(server->TestServerPage("auth-basic")));
  EXPECT_TRUE(tab->NeedsAuth());
  EXPECT_TRUE(tab->GoForward());  // should bring us to 'c'
  EXPECT_FALSE(tab->NeedsAuth());

  // Now test that cancelling works as expected.
  ASSERT_TRUE(tab->NavigateToURL(server->TestServerPage("auth-basic")));
  EXPECT_TRUE(tab->NeedsAuth());
  EXPECT_TRUE(tab->CancelAuth());
  EXPECT_FALSE(tab->NeedsAuth());
  EXPECT_EQ(L"Denied: no auth", GetActiveTabTitle());
}

// If multiple tabs are looking for the same auth, the user should only have to
// enter it once (http://crbug.com/8914).
TEST_F(LoginPromptTest, SupplyRedundantAuths) {
  scoped_refptr<HTTPTestServer> server =
      HTTPTestServer::CreateServer(kDocRoot, NULL);
  ASSERT_TRUE(NULL != server.get());

  scoped_refptr<TabProxy> basic_tab1(GetActiveTab());
  ASSERT_TRUE(basic_tab1.get());
  ASSERT_TRUE(
      basic_tab1->NavigateToURL(server->TestServerPage("auth-basic/1")));
  EXPECT_TRUE(basic_tab1->NeedsAuth());

  AppendTab(GURL(chrome::kAboutBlankURL));
  scoped_refptr<TabProxy> basic_tab2(GetActiveTab());
  ASSERT_TRUE(basic_tab2.get());
  ASSERT_TRUE(
      basic_tab2->NavigateToURL(server->TestServerPage("auth-basic/2")));
  EXPECT_TRUE(basic_tab2->NeedsAuth());

  // Set the auth in only one of the tabs (but wait for the other to load).
  int64 last_navigation_time;
  ASSERT_TRUE(basic_tab2->GetLastNavigationTime(&last_navigation_time));
  EXPECT_TRUE(basic_tab1->SetAuth(username_basic_, password_));
  EXPECT_TRUE(basic_tab2->WaitForNavigation(last_navigation_time));

  // Now both tabs have loaded.
  wstring title1;
  EXPECT_TRUE(basic_tab1->GetTabTitle(&title1));
  EXPECT_EQ(ExpectedTitleFromAuth(username_basic_, password_), title1);
  wstring title2;
  EXPECT_TRUE(basic_tab2->GetTabTitle(&title2));
  EXPECT_EQ(ExpectedTitleFromAuth(username_basic_, password_), title2);
}

// If multiple tabs are looking for the same auth, and one is cancelled, the
// other should be cancelled as well.
TEST_F(LoginPromptTest, CancelRedundantAuths) {
  scoped_refptr<HTTPTestServer> server =
      HTTPTestServer::CreateServer(kDocRoot, NULL);
  ASSERT_TRUE(NULL != server.get());

  scoped_refptr<TabProxy> basic_tab1(GetActiveTab());
  ASSERT_TRUE(basic_tab1.get());
  ASSERT_TRUE(
      basic_tab1->NavigateToURL(server->TestServerPage("auth-basic/1")));
  EXPECT_TRUE(basic_tab1->NeedsAuth());

  AppendTab(GURL(chrome::kAboutBlankURL));
  scoped_refptr<TabProxy> basic_tab2(GetActiveTab());
  ASSERT_TRUE(basic_tab2.get());
  ASSERT_TRUE(
      basic_tab2->NavigateToURL(server->TestServerPage("auth-basic/2")));
  EXPECT_TRUE(basic_tab2->NeedsAuth());

  // Cancel the auth in only one of the tabs (but wait for the other to load).
  int64 last_navigation_time;
  ASSERT_TRUE(basic_tab2->GetLastNavigationTime(&last_navigation_time));
  EXPECT_TRUE(basic_tab1->CancelAuth());
  EXPECT_TRUE(basic_tab2->WaitForNavigation(last_navigation_time));

  // Now both tabs have been denied.
  wstring title1;
  EXPECT_TRUE(basic_tab1->GetTabTitle(&title1));
  EXPECT_EQ(L"Denied: no auth", title1);
  wstring title2;
  EXPECT_TRUE(basic_tab2->GetTabTitle(&title2));
  EXPECT_EQ(L"Denied: no auth", title2);
}
