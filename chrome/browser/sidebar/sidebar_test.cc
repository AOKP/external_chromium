// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "chrome/browser/browser.h"
#include "chrome/browser/browser_window.h"
#include "chrome/browser/sidebar/sidebar_manager.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/browser/views/frame/browser_view.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/in_process_browser_test.h"
#include "chrome/test/ui_test_utils.h"
#include "net/test/test_server.h"

namespace {

const char kSampleContentId[] = "sample_content_id";
const char kSimplePage[] = "files/sidebar/simple_page.html";

class SidebarTest : public InProcessBrowserTest {
 public:
  SidebarTest() {
    CommandLine::ForCurrentProcess()->AppendSwitch(
        switches::kEnableExperimentalExtensionApis);
    set_show_window(true);
  }

 protected:
  void ShowSidebarForCurrentTab() {
    ShowSidebar(browser()->GetSelectedTabContents());
  }

  void ExpandSidebarForCurrentTab() {
    ExpandSidebar(browser()->GetSelectedTabContents());
  }

  void CollapseSidebarForCurrentTab() {
    CollapseSidebar(browser()->GetSelectedTabContents());
  }

  void HideSidebarForCurrentTab() {
    HideSidebar(browser()->GetSelectedTabContents());
  }

  void NavigateSidebarForCurrentTabTo(const std::string& test_page) {
    net::TestServer* server = test_server();
    ASSERT_TRUE(server);
    GURL url = server->GetURL(test_page);

    TabContents* tab = browser()->GetSelectedTabContents();

    SidebarManager* sidebar_manager = SidebarManager::GetInstance();

    sidebar_manager->NavigateSidebar(tab, kSampleContentId, url);

    SidebarContainer* sidebar_container =
        sidebar_manager->GetSidebarContainerFor(tab, kSampleContentId);

    TabContents* client_contents = sidebar_container->sidebar_contents();
    ui_test_utils::WaitForNavigation(&client_contents->controller());
  }

  void ShowSidebar(TabContents* tab) {
    SidebarManager* sidebar_manager = SidebarManager::GetInstance();
    sidebar_manager->ShowSidebar(tab, kSampleContentId);
  }

  void ExpandSidebar(TabContents* tab) {
    SidebarManager* sidebar_manager = SidebarManager::GetInstance();
    sidebar_manager->ExpandSidebar(tab, kSampleContentId);
    if (browser()->GetSelectedTabContents() == tab)
      EXPECT_GT(browser_view()->GetSidebarWidth(), 0);
  }

  void CollapseSidebar(TabContents* tab) {
    SidebarManager* sidebar_manager = SidebarManager::GetInstance();
    sidebar_manager->CollapseSidebar(tab, kSampleContentId);
    if (browser()->GetSelectedTabContents() == tab)
      EXPECT_EQ(0, browser_view()->GetSidebarWidth());
  }

  void HideSidebar(TabContents* tab) {
    SidebarManager* sidebar_manager = SidebarManager::GetInstance();
    sidebar_manager->HideSidebar(tab, kSampleContentId);
    if (browser()->GetSelectedTabContents() == tab)
      EXPECT_EQ(0, browser_view()->GetSidebarWidth());
  }

  TabContents* tab_contents(int i) {
    return browser()->GetTabContentsAt(i);
  }

  BrowserView* browser_view() const {
    return static_cast<BrowserView*>(browser()->window());
  }
};

IN_PROC_BROWSER_TEST_F(SidebarTest, OpenClose) {
  ShowSidebarForCurrentTab();

  ExpandSidebarForCurrentTab();
  CollapseSidebarForCurrentTab();

  ExpandSidebarForCurrentTab();
  CollapseSidebarForCurrentTab();

  ExpandSidebarForCurrentTab();
  CollapseSidebarForCurrentTab();

  HideSidebarForCurrentTab();

  ShowSidebarForCurrentTab();

  ExpandSidebarForCurrentTab();
  CollapseSidebarForCurrentTab();

  HideSidebarForCurrentTab();
}

IN_PROC_BROWSER_TEST_F(SidebarTest, SwitchingTabs) {
  ShowSidebarForCurrentTab();
  ExpandSidebarForCurrentTab();

  browser()->NewTab();

  // Make sure sidebar is not visbile for the newly opened tab.
  EXPECT_EQ(0, browser_view()->GetSidebarWidth());

  // Switch back to the first tab.
  browser()->SelectNumberedTab(0);

  // Make sure it is visible now.
  EXPECT_GT(browser_view()->GetSidebarWidth(), 0);

  HideSidebarForCurrentTab();
}

IN_PROC_BROWSER_TEST_F(SidebarTest, SidebarOnInactiveTab) {
  ShowSidebarForCurrentTab();
  ExpandSidebarForCurrentTab();

  browser()->NewTab();

  // Hide sidebar on inactive (first) tab.
  HideSidebar(tab_contents(0));

  // Switch back to the first tab.
  browser()->SelectNumberedTab(0);

  // Make sure sidebar is not visbile anymore.
  EXPECT_EQ(0, browser_view()->GetSidebarWidth());

  // Show sidebar on inactive (second) tab.
  ShowSidebar(tab_contents(1));
  ExpandSidebar(tab_contents(1));
  // Make sure sidebar is not visible yet.
  EXPECT_EQ(0, browser_view()->GetSidebarWidth());

  // Switch back to the second tab.
  browser()->SelectNumberedTab(1);
  // Make sure sidebar is visible now.
  EXPECT_GT(browser_view()->GetSidebarWidth(), 0);

  HideSidebarForCurrentTab();
}

// FAILS, http://crbug.com/57964
#if defined(OS_WIN)
#define MAYBE_SidebarNavigate DISABLED_SidebarNavigate
#else
#define MAYBE_SidebarNavigate SidebarNavigate
#endif
IN_PROC_BROWSER_TEST_F(SidebarTest, MAYBE_SidebarNavigate) {
  ShowSidebarForCurrentTab();

  NavigateSidebarForCurrentTabTo(kSimplePage);

  HideSidebarForCurrentTab();
}

}  // namespace

