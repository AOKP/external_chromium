// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/keyboard_codes.h"
#include "base/message_loop.h"
#include "chrome/browser/automation/ui_controls.h"
#include "chrome/browser/browser.h"
#include "chrome/browser/browser_window.h"
#include "chrome/browser/find_bar_controller.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/browser/views/find_bar_host.h"
#include "chrome/browser/views/frame/browser_view.h"
#include "chrome/browser/view_ids.h"
#include "chrome/test/in_process_browser_test.h"
#include "chrome/test/ui_test_utils.h"
#include "net/url_request/url_request_unittest.h"
#include "views/focus/focus_manager.h"
#include "views/view.h"

namespace {

// The delay waited after sending an OS simulated event.
static const int kActionDelayMs = 500;
static const wchar_t kDocRoot[] = L"chrome/test/data";
static const char kSimplePage[] = "files/find_in_page/simple.html";

class FindInPageTest : public InProcessBrowserTest {
 public:
  FindInPageTest() {
    set_show_window(true);
    FindBarHost::disable_animations_during_testing_ = true;
  }

  void ClickOnView(ViewID view_id) {
    BrowserWindow* browser_window = browser()->window();
    ASSERT_TRUE(browser_window);
#if defined(TOOLKIT_VIEWS)
    views::View* view =
        reinterpret_cast<BrowserView*>(browser_window)->GetViewByID(view_id);
#elif defined(OS_LINUX)
    gfx::NativeWindow window = browser_window->GetNativeHandle();
    ASSERT_TRUE(window);
    GtkWidget* view = ViewIDUtil::GetWidget(GTK_WIDGET(window), view_id);
#endif
    ASSERT_TRUE(view);
    ui_controls::MoveMouseToCenterAndPress(view,
                                           ui_controls::LEFT,
                                           ui_controls::DOWN | ui_controls::UP,
                                           new MessageLoop::QuitTask());
    ui_test_utils::RunMessageLoop();
  }

  int GetFocusedViewID() {
#if defined(TOOLKIT_VIEWS)
#if defined(OS_LINUX)
    // See http://crbug.com/26873 .
    views::FocusManager* focus_manager =
        views::FocusManager::GetFocusManagerForNativeView(
            GTK_WIDGET(browser()->window()->GetNativeHandle()));
#else
    views::FocusManager* focus_manager =
        views::FocusManager::GetFocusManagerForNativeView(
            browser()->window()->GetNativeHandle());
#endif

    if (!focus_manager) {
      NOTREACHED();
      return -1;
    }
    views::View* focused_view = focus_manager->GetFocusedView();
    if (!focused_view)
      return -1;
    return focused_view->GetID();
#else
    return -1;
#endif
  }

  string16 GetFindBarText() {
    FindBarTesting* find_bar =
        browser()->GetFindBarController()->find_bar()->GetFindBarTesting();
    return find_bar->GetFindText();
  }
};

}  // namespace

IN_PROC_BROWSER_TEST_F(FindInPageTest, CrashEscHandlers) {
  scoped_refptr<HTTPTestServer> server =
      HTTPTestServer::CreateServer(kDocRoot, NULL);
  ASSERT_TRUE(NULL != server.get());

  // First we navigate to our test page (tab A).
  GURL url = server->TestServerPage(kSimplePage);
  ui_test_utils::NavigateToURL(browser(), url);

  browser()->Find();

  // Open another tab (tab B).
  browser()->AddTabWithURL(url, GURL(), PageTransition::TYPED, -1,
                           TabStripModel::ADD_SELECTED, NULL, std::string());

  browser()->Find();
  EXPECT_EQ(VIEW_ID_FIND_IN_PAGE_TEXT_FIELD, GetFocusedViewID());

  // Select tab A.
  browser()->SelectTabContentsAt(0, true);

  // Close tab B.
  browser()->CloseTabContents(browser()->GetTabContentsAt(1));

  // Click on the location bar so that Find box loses focus.
  ClickOnView(VIEW_ID_LOCATION_BAR);
#if defined(TOOLKIT_VIEWS) || defined(OS_WIN)
  // Check the location bar is focused.
  EXPECT_EQ(VIEW_ID_LOCATION_BAR, GetFocusedViewID());
#endif

  // This used to crash until bug 1303709 was fixed.
  ui_controls::SendKeyPressNotifyWhenDone(
      browser()->window()->GetNativeHandle(), base::VKEY_ESCAPE,
      false, false, false, false, new MessageLoop::QuitTask());
  ui_test_utils::RunMessageLoop();
}

IN_PROC_BROWSER_TEST_F(FindInPageTest, FocusRestore) {
  scoped_refptr<HTTPTestServer> server =
      HTTPTestServer::CreateServer(kDocRoot, NULL);
  ASSERT_TRUE(NULL != server.get());

  GURL url = server->TestServerPage("title1.html");
  ui_test_utils::NavigateToURL(browser(), url);

  // Focus the location bar, open and close the find-in-page, focus should
  // return to the location bar.
  browser()->FocusLocationBar();
  EXPECT_EQ(VIEW_ID_LOCATION_BAR, GetFocusedViewID());
  // Ensure the creation of the find bar controller.
  browser()->GetFindBarController()->Show();
  EXPECT_EQ(VIEW_ID_FIND_IN_PAGE_TEXT_FIELD, GetFocusedViewID());
  browser()->GetFindBarController()->EndFindSession(
      FindBarController::kKeepSelection);
  EXPECT_EQ(VIEW_ID_LOCATION_BAR, GetFocusedViewID());

  // Focus the location bar, find something on the page, close the find box,
  // focus should go to the page.
  browser()->FocusLocationBar();
  browser()->Find();
  EXPECT_EQ(VIEW_ID_FIND_IN_PAGE_TEXT_FIELD, GetFocusedViewID());
  ui_test_utils::FindInPage(browser()->GetSelectedTabContents(),
                            ASCIIToUTF16("a"), true, false, NULL);
  browser()->GetFindBarController()->EndFindSession(
      FindBarController::kKeepSelection);
  EXPECT_EQ(VIEW_ID_TAB_CONTAINER_FOCUS_VIEW, GetFocusedViewID());

  // Focus the location bar, open and close the find box, focus should return to
  // the location bar (same as before, just checking that http://crbug.com/23599
  // is fixed).
  browser()->FocusLocationBar();
  EXPECT_EQ(VIEW_ID_LOCATION_BAR, GetFocusedViewID());
  browser()->GetFindBarController()->Show();
  EXPECT_EQ(VIEW_ID_FIND_IN_PAGE_TEXT_FIELD, GetFocusedViewID());
  browser()->GetFindBarController()->EndFindSession(
      FindBarController::kKeepSelection);
  EXPECT_EQ(VIEW_ID_LOCATION_BAR, GetFocusedViewID());
}

// This tests that whenever you clear values from the Find box and close it that
// it respects that and doesn't show you the last search, as reported in bug:
// http://crbug.com/40121.
IN_PROC_BROWSER_TEST_F(FindInPageTest, PrepopulateRespectBlank) {
#if defined(OS_MACOSX)
  // FindInPage on Mac doesn't use prepopulated values. Search there is global.
  return;
#endif

  HTTPTestServer* server = StartHTTPServer();
  ASSERT_TRUE(server);

  // First we navigate to any page.
  GURL url = server->TestServerPage(kSimplePage);
  ui_test_utils::NavigateToURL(browser(), url);

  gfx::NativeWindow window = browser()->window()->GetNativeHandle();

  // Show the Find bar.
  browser()->GetFindBarController()->Show();

  // Search for "a".
  ui_controls::SendKeyPressNotifyWhenDone(window, base::VKEY_A,
      false, false, false, false,  // No modifiers.
      new MessageLoop::QuitTask());
  ui_test_utils::RunMessageLoop();

  // We should find "a" here.
  EXPECT_EQ(ASCIIToUTF16("a"), GetFindBarText());

  // Delete "a".
  ui_controls::SendKeyPressNotifyWhenDone(window, base::VKEY_BACK,
      false, false, false, false,  // No modifiers.
      new MessageLoop::QuitTask());
  ui_test_utils::RunMessageLoop();

  // Validate we have cleared the text.
  EXPECT_EQ(string16(), GetFindBarText());

  // Close the Find box.
  ui_controls::SendKeyPressNotifyWhenDone(window, base::VKEY_ESCAPE,
      false, false, false, false,  // No modifiers.
      new MessageLoop::QuitTask());
  ui_test_utils::RunMessageLoop();

  // Show the Find bar.
  browser()->GetFindBarController()->Show();

  // After the Find box has been reopened, it should not have been prepopulated
  // with "a" again.
  EXPECT_EQ(string16(), GetFindBarText());

  // Close the Find box.
  ui_controls::SendKeyPressNotifyWhenDone(window, base::VKEY_ESCAPE,
      false, false, false, false,  // No modifiers.
      new MessageLoop::QuitTask());
  ui_test_utils::RunMessageLoop();

  // Press F3 to trigger FindNext.
  ui_controls::SendKeyPressNotifyWhenDone(window, base::VKEY_F3,
      false, false, false, false,  // No modifiers.
      new MessageLoop::QuitTask());
  ui_test_utils::RunMessageLoop();

  // After the Find box has been reopened, it should still have no prepopulate
  // value.
  EXPECT_EQ(string16(), GetFindBarText());
}
