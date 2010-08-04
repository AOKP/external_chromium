// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/keyboard_codes.h"
#include "base/message_loop.h"
#include "chrome/browser/browser.h"
#include "chrome/browser/browser_window.h"
#include "chrome/browser/find_bar.h"
#include "chrome/browser/find_bar_controller.h"
#include "chrome/browser/find_notification_details.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/renderer_host/render_view_host.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/browser/tab_contents/tab_contents_view.h"
#include "chrome/common/notification_service.h"
#include "chrome/test/in_process_browser_test.h"
#include "chrome/test/ui_test_utils.h"

#if defined(TOOLKIT_VIEWS)
#include "chrome/browser/views/find_bar_host.h"
#include "views/focus/focus_manager.h"
#elif defined(TOOLKIT_GTK)
#include "chrome/browser/gtk/slide_animator_gtk.h"
#elif defined(OS_MACOSX)
#include "chrome/browser/cocoa/find_bar_bridge.h"
#endif

const std::string kSimplePage = "404_is_enough_for_us.html";
const std::string kFramePage = "files/find_in_page/frames.html";
const std::string kFrameData = "files/find_in_page/framedata_general.html";
const std::string kUserSelectPage = "files/find_in_page/user-select.html";
const std::string kCrashPage = "files/find_in_page/crash_1341577.html";
const std::string kTooFewMatchesPage = "files/find_in_page/bug_1155639.html";
const std::string kEndState = "files/find_in_page/end_state.html";
const std::string kPrematureEnd = "files/find_in_page/premature_end.html";
const std::string kMoveIfOver = "files/find_in_page/move_if_obscuring.html";
const std::string kBitstackCrash = "files/find_in_page/crash_14491.html";
const std::string kSelectChangesOrdinal =
    "files/find_in_page/select_changes_ordinal.html";
const std::string kSimple = "files/find_in_page/simple.html";
const std::string kLinkPage = "files/find_in_page/link.html";

const bool kBack = false;
const bool kFwd = true;

const bool kIgnoreCase = false;
const bool kCaseSensitive = true;

const int kMoveIterations = 30;

class FindInPageControllerTest : public InProcessBrowserTest {
 public:
  FindInPageControllerTest() {
    EnableDOMAutomation();

#if defined(TOOLKIT_VIEWS)
    DropdownBarHost::disable_animations_during_testing_ = true;
#elif defined(TOOLKIT_GTK)
    SlideAnimatorGtk::SetAnimationsForTesting(false);
#elif defined(OS_MACOSX)
    FindBarBridge::disable_animations_during_testing_ = true;
#endif
  }

 protected:
  bool GetFindBarWindowInfoForBrowser(
      Browser* browser, gfx::Point* position, bool* fully_visible) {
    FindBarTesting* find_bar =
        browser->GetFindBarController()->find_bar()->GetFindBarTesting();
    return find_bar->GetFindBarWindowInfo(position, fully_visible);
  }

  bool GetFindBarWindowInfo(gfx::Point* position, bool* fully_visible) {
    return GetFindBarWindowInfoForBrowser(browser(), position, fully_visible);
  }

  string16 GetFindBarTextForBrowser(Browser* browser) {
    FindBarTesting* find_bar =
        browser->GetFindBarController()->find_bar()->GetFindBarTesting();
    return find_bar->GetFindText();
  }

  string16 GetFindBarText() {
    return GetFindBarTextForBrowser(browser());
  }

  void EnsureFindBoxOpenForBrowser(Browser* browser) {
    browser->ShowFindBar();
    gfx::Point position;
    bool fully_visible = false;
    EXPECT_TRUE(GetFindBarWindowInfoForBrowser(
                    browser, &position, &fully_visible));
    EXPECT_TRUE(fully_visible);
  }

  void EnsureFindBoxOpen() {
    EnsureFindBoxOpenForBrowser(browser());
  }
};

// Platform independent FindInPage that takes |const wchar_t*|
// as an input.
int FindInPageWchar(TabContents* tab,
                    const wchar_t* search_str,
                    bool forward,
                    bool case_sensitive,
                    int* ordinal) {
  return ui_test_utils::FindInPage(
      tab, WideToUTF16(std::wstring(search_str)),
      forward, case_sensitive, ordinal);
}

// This test loads a page with frames and starts FindInPage requests.
IN_PROC_BROWSER_TEST_F(FindInPageControllerTest, FindInPageFrames) {
  HTTPTestServer* server = StartHTTPServer();
  ASSERT_TRUE(server);

  // First we navigate to our frames page.
  GURL url = server->TestServerPage(kFramePage);
  ui_test_utils::NavigateToURL(browser(), url);

  // Try incremental search (mimicking user typing in).
  int ordinal = 0;
  TabContents* tab = browser()->GetSelectedTabContents();
  EXPECT_EQ(18, FindInPageWchar(tab, L"g",
                                kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(1, ordinal);
  EXPECT_EQ(11, FindInPageWchar(tab, L"go",
                                kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(1, ordinal);
  EXPECT_EQ(04, FindInPageWchar(tab, L"goo",
                                kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(1, ordinal);
  EXPECT_EQ(03, FindInPageWchar(tab, L"goog",
                                kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(1, ordinal);
  EXPECT_EQ(02, FindInPageWchar(tab, L"googl",
                                kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(1, ordinal);
  EXPECT_EQ(01, FindInPageWchar(tab, L"google",
                                kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(1, ordinal);
  EXPECT_EQ(00, FindInPageWchar(tab, L"google!",
                                kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(0, ordinal);

  // Negative test (no matches should be found).
  EXPECT_EQ(0, FindInPageWchar(tab, L"Non-existing string",
                               kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(0, ordinal);

  // 'horse' only exists in the three right frames.
  EXPECT_EQ(3, FindInPageWchar(tab, L"horse",
                               kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(1, ordinal);

  // 'cat' only exists in the first frame.
  EXPECT_EQ(1, FindInPageWchar(tab, L"cat",
                               kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(1, ordinal);

  // Try searching again, should still come up with 1 match.
  EXPECT_EQ(1, FindInPageWchar(tab, L"cat",
                               kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(1, ordinal);

  // Try searching backwards, ignoring case, should still come up with 1 match.
  EXPECT_EQ(1, FindInPageWchar(tab, L"CAT",
                               kBack, kIgnoreCase, &ordinal));
  EXPECT_EQ(1, ordinal);

  // Try case sensitive, should NOT find it.
  EXPECT_EQ(0, FindInPageWchar(tab, L"CAT",
                               kFwd, kCaseSensitive, &ordinal));
  EXPECT_EQ(0, ordinal);

  // Try again case sensitive, but this time with right case.
  EXPECT_EQ(1, FindInPageWchar(tab, L"dog",
                               kFwd, kCaseSensitive, &ordinal));
  EXPECT_EQ(1, ordinal);

  // Try non-Latin characters ('Hreggvidur' with 'eth' for 'd' in left frame).
  EXPECT_EQ(1, FindInPageWchar(tab, L"Hreggvi\u00F0ur",
                               kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(1, ordinal);
  EXPECT_EQ(1, FindInPageWchar(tab, L"Hreggvi\u00F0ur",
                               kFwd, kCaseSensitive, &ordinal));
  EXPECT_EQ(1, ordinal);
  EXPECT_EQ(0, FindInPageWchar(tab, L"hreggvi\u00F0ur",
                               kFwd, kCaseSensitive, &ordinal));
  EXPECT_EQ(0, ordinal);
}

std::string FocusedOnPage(TabContents* tab_contents) {
  std::string result;
  ui_test_utils::ExecuteJavaScriptAndExtractString(
      tab_contents->render_view_host(),
      L"",
      L"window.domAutomationController.send(getFocusedElement());",
      &result);
  return result;
}

// This tests the FindInPage end-state, in other words: what is focused when you
// close the Find box (ie. if you find within a link the link should be
// focused).
IN_PROC_BROWSER_TEST_F(FindInPageControllerTest, FindInPageEndState) {
  HTTPTestServer* server = StartHTTPServer();
  ASSERT_TRUE(server);

  // First we navigate to our special focus tracking page.
  GURL url = server->TestServerPage(kEndState);
  ui_test_utils::NavigateToURL(browser(), url);

  TabContents* tab_contents = browser()->GetSelectedTabContents();
  ASSERT_TRUE(NULL != tab_contents);

  // Verify that nothing has focus.
  ASSERT_STREQ("{nothing focused}", FocusedOnPage(tab_contents).c_str());

  // Search for a text that exists within a link on the page.
  int ordinal = 0;
  EXPECT_EQ(1, FindInPageWchar(tab_contents, L"nk",
                               kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(1, ordinal);

  // End the find session, which should set focus to the link.
  tab_contents->StopFinding(FindBarController::kKeepSelection);

  // Verify that the link is focused.
  EXPECT_STREQ("link1", FocusedOnPage(tab_contents).c_str());

  // Search for a text that exists within a link on the page.
  EXPECT_EQ(1, FindInPageWchar(tab_contents, L"Google",
                               kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(1, ordinal);

  // Move the selection to link 1, after searching.
  std::string result;
  ui_test_utils::ExecuteJavaScriptAndExtractString(
      tab_contents->render_view_host(),
      L"",
      L"window.domAutomationController.send(selectLink1());",
      &result);

  // End the find session.
  tab_contents->StopFinding(FindBarController::kKeepSelection);

  // Verify that link2 is not focused.
  EXPECT_STREQ("", FocusedOnPage(tab_contents).c_str());
}

// This test loads a single-frame page and makes sure the ordinal returned makes
// sense as we FindNext over all the items.
IN_PROC_BROWSER_TEST_F(FindInPageControllerTest, FindInPageOrdinal) {
  HTTPTestServer* server = StartHTTPServer();
  ASSERT_TRUE(server);

  // First we navigate to our page.
  GURL url = server->TestServerPage(kFrameData);
  ui_test_utils::NavigateToURL(browser(), url);

  // Search for 'o', which should make the first item active and return
  // '1 in 3' (1st ordinal of a total of 3 matches).
  TabContents* tab = browser()->GetSelectedTabContents();
  int ordinal = 0;
  EXPECT_EQ(3, FindInPageWchar(tab, L"o",
                               kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(1, ordinal);
  EXPECT_EQ(3, FindInPageWchar(tab, L"o",
                               kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(2, ordinal);
  EXPECT_EQ(3, FindInPageWchar(tab, L"o",
                               kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(3, ordinal);
  // Go back one match.
  EXPECT_EQ(3, FindInPageWchar(tab, L"o",
                               kBack, kIgnoreCase, &ordinal));
  EXPECT_EQ(2, ordinal);
  EXPECT_EQ(3, FindInPageWchar(tab, L"o",
                               kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(3, ordinal);
  // This should wrap to the top.
  EXPECT_EQ(3, FindInPageWchar(tab, L"o",
                               kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(1, ordinal);
  // This should go back to the end.
  EXPECT_EQ(3, FindInPageWchar(tab, L"o",
                               kBack, kIgnoreCase, &ordinal));
  EXPECT_EQ(3, ordinal);
}

// This tests that the ordinal is correctly adjusted after a selection
IN_PROC_BROWSER_TEST_F(FindInPageControllerTest,
                       SelectChangesOrdinal_Issue20883) {
  HTTPTestServer* server = StartHTTPServer();
  ASSERT_TRUE(server);

  // First we navigate to our test content.
  GURL url = server->TestServerPage(kSelectChangesOrdinal);
  ui_test_utils::NavigateToURL(browser(), url);

  TabContents* tab_contents = browser()->GetSelectedTabContents();
  ASSERT_TRUE(NULL != tab_contents);

  // Search for a text that exists within a link on the page.
  TabContents* tab = browser()->GetSelectedTabContents();
  int ordinal = 0;
  EXPECT_EQ(4, FindInPageWchar(tab_contents,
                               L"google",
                               kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(1, ordinal);

  // Move the selection to link 1, after searching.
  std::string result;
  ui_test_utils::ExecuteJavaScriptAndExtractString(
      tab_contents->render_view_host(),
      L"",
      L"window.domAutomationController.send(selectLink1());",
      &result);

  // Do a find-next after the selection.  This should move forward
  // from there to the 3rd instance of 'google'.
  EXPECT_EQ(4, FindInPageWchar(tab,
                               L"google",
                               kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(3, ordinal);

  // End the find session.
  tab_contents->StopFinding(FindBarController::kKeepSelection);
}

// This test loads a page with frames and makes sure the ordinal returned makes
// sense.
IN_PROC_BROWSER_TEST_F(FindInPageControllerTest, FindInPageMultiFramesOrdinal) {
  HTTPTestServer* server = StartHTTPServer();
  ASSERT_TRUE(server);

  // First we navigate to our page.
  GURL url = server->TestServerPage(kFramePage);
  ui_test_utils::NavigateToURL(browser(), url);

  // Search for 'a', which should make the first item active and return
  // '1 in 7' (1st ordinal of a total of 7 matches).
  TabContents* tab = browser()->GetSelectedTabContents();
  int ordinal = 0;
  EXPECT_EQ(7,
            FindInPageWchar(tab, L"a", kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(1, ordinal);
  EXPECT_EQ(7,
            FindInPageWchar(tab, L"a", kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(2, ordinal);
  EXPECT_EQ(7,
            FindInPageWchar(tab, L"a", kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(3, ordinal);
  EXPECT_EQ(7,
            FindInPageWchar(tab, L"a", kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(4, ordinal);
  // Go back one, which should go back one frame.
  EXPECT_EQ(7,
            FindInPageWchar(tab, L"a", kBack, kIgnoreCase, &ordinal));
  EXPECT_EQ(3, ordinal);
  EXPECT_EQ(7,
            FindInPageWchar(tab, L"a", kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(4, ordinal);
  EXPECT_EQ(7,
            FindInPageWchar(tab, L"a", kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(5, ordinal);
  EXPECT_EQ(7,
            FindInPageWchar(tab, L"a", kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(6, ordinal);
  EXPECT_EQ(7,
            FindInPageWchar(tab, L"a", kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(7, ordinal);
  // Now we should wrap back to frame 1.
  EXPECT_EQ(7,
            FindInPageWchar(tab, L"a", kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(1, ordinal);
  // Now we should wrap back to frame last frame.
  EXPECT_EQ(7,
            FindInPageWchar(tab, L"a", kBack, kIgnoreCase, &ordinal));
  EXPECT_EQ(7, ordinal);
}

// We could get ordinals out of whack when restarting search in subframes.
// See http://crbug.com/5132.
IN_PROC_BROWSER_TEST_F(FindInPageControllerTest, FindInPage_Issue5132) {
  HTTPTestServer* server = StartHTTPServer();
  ASSERT_TRUE(server);

  // First we navigate to our page.
  GURL url = server->TestServerPage(kFramePage);
  ui_test_utils::NavigateToURL(browser(), url);

  // Search for 'goa' three times (6 matches on page).
  int ordinal = 0;
  TabContents* tab = browser()->GetSelectedTabContents();
  EXPECT_EQ(6, FindInPageWchar(tab, L"goa",
                               kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(1, ordinal);
  EXPECT_EQ(6, FindInPageWchar(tab, L"goa",
                               kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(2, ordinal);
  EXPECT_EQ(6, FindInPageWchar(tab, L"goa",
                               kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(3, ordinal);
  // Add space to search (should result in no matches).
  EXPECT_EQ(0, FindInPageWchar(tab, L"goa ",
                               kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(0, ordinal);
  // Remove the space, should be back to '3 out of 6')
  EXPECT_EQ(6, FindInPageWchar(tab, L"goa",
                               kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(3, ordinal);
}

// Load a page with no selectable text and make sure we don't crash.
IN_PROC_BROWSER_TEST_F(FindInPageControllerTest, FindUnSelectableText) {
  HTTPTestServer* server = StartHTTPServer();
  ASSERT_TRUE(server);

  // First we navigate to our page.
  GURL url = server->TestServerPage(kUserSelectPage);
  ui_test_utils::NavigateToURL(browser(), url);

  int ordinal = 0;
  TabContents* tab = browser()->GetSelectedTabContents();
  EXPECT_EQ(0, FindInPageWchar(tab, L"text",
                               kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(-1, ordinal);  // Nothing is selected.
  EXPECT_EQ(0, FindInPageWchar(tab, L"Non-existing string",
                               kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(0, ordinal);
}

// Try to reproduce the crash seen in issue 1341577.
IN_PROC_BROWSER_TEST_F(FindInPageControllerTest, FindCrash_Issue1341577) {
  HTTPTestServer* server = StartHTTPServer();
  ASSERT_TRUE(server);

  // First we navigate to our page.
  GURL url = server->TestServerPage(kCrashPage);
  ui_test_utils::NavigateToURL(browser(), url);

  // This would crash the tab. These must be the first two find requests issued
  // against the frame, otherwise an active frame pointer is set and it wont
  // produce the crash.
  // We used to check the return value and |ordinal|. With ICU 4.2, FiP does
  // not find a stand-alone dependent vowel sign of Indic scripts. So, the
  // exptected values are all 0. To make this test pass regardless of
  // ICU version, we just call FiP and see if there's any crash.
  // TODO(jungshik): According to a native Malayalam speaker, it's ok not
  // to find U+0D4C. Still need to investigate further this issue.
  int ordinal = 0;
  TabContents* tab = browser()->GetSelectedTabContents();
  FindInPageWchar(tab, L"\u0D4C", kFwd, kIgnoreCase, &ordinal);
  FindInPageWchar(tab, L"\u0D4C", kFwd, kIgnoreCase, &ordinal);

  // This should work fine.
  EXPECT_EQ(1, FindInPageWchar(tab, L"\u0D24\u0D46",
                               kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(1, ordinal);
  EXPECT_EQ(0, FindInPageWchar(tab, L"nostring",
                               kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(0, ordinal);
}

// Try to reproduce the crash seen in http://crbug.com/14491, where an assert
// hits in the BitStack size comparison in WebKit.
IN_PROC_BROWSER_TEST_F(FindInPageControllerTest, FindCrash_Issue14491) {
  HTTPTestServer* server = StartHTTPServer();
  ASSERT_TRUE(server);

  // First we navigate to our page.
  GURL url = server->TestServerPage(kBitstackCrash);
  ui_test_utils::NavigateToURL(browser(), url);

  // This used to crash the tab.
  int ordinal = 0;
  EXPECT_EQ(0, FindInPageWchar(browser()->GetSelectedTabContents(),
                               L"s", kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(0, ordinal);
}

// Test to make sure Find does the right thing when restarting from a timeout.
// We used to have a problem where we'd stop finding matches when all of the
// following conditions were true:
// 1) The page has a lot of text to search.
// 2) The page contains more than one match.
// 3) It takes longer than the time-slice given to each Find operation (100
//    ms) to find one or more of those matches (so Find times out and has to try
//    again from where it left off).
IN_PROC_BROWSER_TEST_F(FindInPageControllerTest, FindRestarts_Issue1155639) {
  HTTPTestServer* server = StartHTTPServer();
  ASSERT_TRUE(server);

  // First we navigate to our page.
  GURL url = server->TestServerPage(kTooFewMatchesPage);
  ui_test_utils::NavigateToURL(browser(), url);

  // This string appears 5 times at the bottom of a long page. If Find restarts
  // properly after a timeout, it will find 5 matches, not just 1.
  int ordinal = 0;
  EXPECT_EQ(5, FindInPageWchar(browser()->GetSelectedTabContents(),
                               L"008.xml",
                               kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(1, ordinal);
}

// This tests bug 11761: FindInPage terminates search prematurely.
IN_PROC_BROWSER_TEST_F(FindInPageControllerTest, FAILS_FindInPagePrematureEnd) {
  HTTPTestServer* server = StartHTTPServer();
  ASSERT_TRUE(server);

  // First we navigate to our special focus tracking page.
  GURL url = server->TestServerPage(kPrematureEnd);
  ui_test_utils::NavigateToURL(browser(), url);

  TabContents* tab_contents = browser()->GetSelectedTabContents();
  ASSERT_TRUE(NULL != tab_contents);

  // Search for a text that exists within a link on the page.
  int ordinal = 0;
  EXPECT_EQ(2, FindInPageWchar(tab_contents, L"html ",
                               kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(1, ordinal);
}

IN_PROC_BROWSER_TEST_F(FindInPageControllerTest, FindDisappearOnNavigate) {
  HTTPTestServer* server = StartHTTPServer();
  ASSERT_TRUE(server);

  // First we navigate to our special focus tracking page.
  GURL url = server->TestServerPage(kSimplePage);
  GURL url2 = server->TestServerPage(kFramePage);
  ui_test_utils::NavigateToURL(browser(), url);

  browser()->ShowFindBar();

  gfx::Point position;
  bool fully_visible = false;

  // Make sure it is open.
  EXPECT_TRUE(GetFindBarWindowInfo(&position, &fully_visible));
  EXPECT_TRUE(fully_visible);

  // Reload the tab and make sure Find window doesn't go away.
  browser()->Reload(CURRENT_TAB);
  ui_test_utils::WaitForNavigationInCurrentTab(browser());

  EXPECT_TRUE(GetFindBarWindowInfo(&position, &fully_visible));
  EXPECT_TRUE(fully_visible);

  // Navigate and make sure the Find window goes away.
  ui_test_utils::NavigateToURL(browser(), url2);

  EXPECT_TRUE(GetFindBarWindowInfo(&position, &fully_visible));
  EXPECT_FALSE(fully_visible);
}

#if defined(OS_MACOSX)
// FindDisappearOnNewTabAndHistory is flaky, at least on Mac.
// See http://crbug.com/43072
#define FindDisappearOnNewTabAndHistory FLAKY_FindDisappearOnNewTabAndHistory
#endif

// Make sure Find box disappears when History/Downloads page is opened, and
// when a New Tab is opened.
IN_PROC_BROWSER_TEST_F(FindInPageControllerTest,
                       FindDisappearOnNewTabAndHistory) {
  HTTPTestServer* server = StartHTTPServer();
  ASSERT_TRUE(server);

  // First we navigate to our special focus tracking page.
  GURL url = server->TestServerPage(kSimplePage);
  ui_test_utils::NavigateToURL(browser(), url);

  browser()->ShowFindBar();

  gfx::Point position;
  bool fully_visible = false;

  // Make sure it is open.
  EXPECT_TRUE(GetFindBarWindowInfo(&position, &fully_visible));
  EXPECT_TRUE(fully_visible);

  // Open another tab (tab B).
  browser()->NewTab();
  ui_test_utils::NavigateToURL(browser(), url);

  // Make sure Find box is closed.
  EXPECT_TRUE(GetFindBarWindowInfo(&position, &fully_visible));
  EXPECT_FALSE(fully_visible);

  // Close tab B.
  browser()->CloseTab();

  // Make sure Find window appears again.
  EXPECT_TRUE(GetFindBarWindowInfo(&position, &fully_visible));
  EXPECT_TRUE(fully_visible);

  browser()->ShowHistoryTab();

  // Make sure Find box is closed.
  EXPECT_TRUE(GetFindBarWindowInfo(&position, &fully_visible));
  EXPECT_FALSE(fully_visible);
}

// TODO(rohitrao): The FindMovesWhenObscuring test does not pass on mac.
// http://crbug.com/22036
#if defined(OS_MACOSX)
#define MAYBE_FindMovesWhenObscuring FAILS_FindMovesWhenObscuring
#else
#define MAYBE_FindMovesWhenObscuring FindMovesWhenObscuring
#endif

// Make sure Find box moves out of the way if it is obscuring the active match.
IN_PROC_BROWSER_TEST_F(FindInPageControllerTest, MAYBE_FindMovesWhenObscuring) {
  HTTPTestServer* server = StartHTTPServer();
  ASSERT_TRUE(server);

  GURL url = server->TestServerPage(kMoveIfOver);
  ui_test_utils::NavigateToURL(browser(), url);

  browser()->ShowFindBar();

  // This is needed on GTK because the reposition operation is asynchronous.
  MessageLoop::current()->RunAllPending();

  gfx::Point start_position;
  gfx::Point position;
  bool fully_visible = false;

  // Make sure it is open.
  EXPECT_TRUE(GetFindBarWindowInfo(&start_position, &fully_visible));
  EXPECT_TRUE(fully_visible);

  // Search for 'Chromium' which the Find box is obscuring.
  int ordinal = 0;
  TabContents* tab = browser()->GetSelectedTabContents();
  int index = 0;
  for (; index < kMoveIterations; ++index) {
    EXPECT_EQ(kMoveIterations, FindInPageWchar(tab, L"Chromium",
                                               kFwd, kIgnoreCase, &ordinal));

    // Check the position.
    EXPECT_TRUE(GetFindBarWindowInfo(&position, &fully_visible));
    EXPECT_TRUE(fully_visible);

    // If the Find box has moved then we are done.
    if (position.x() != start_position.x())
      break;
  }

  // We should not have reached the end.
  ASSERT_GT(kMoveIterations, index);

  // Search for something guaranteed not to be obscured by the Find box.
  EXPECT_EQ(1, FindInPageWchar(tab, L"Done",
                               kFwd, kIgnoreCase, &ordinal));
  // Check the position.
  EXPECT_TRUE(GetFindBarWindowInfo(&position, &fully_visible));
  EXPECT_TRUE(fully_visible);

  // Make sure Find box has moved back to its original location.
  EXPECT_EQ(position.x(), start_position.x());
}

#if defined(OS_MACOSX)
// FindNextInNewTabUsesPrepopulate times-out, at least on Mac.
// See http://crbug.com/43070
#define FindNextInNewTabUsesPrepopulate DISABLED_FindNextInNewTabUsesPrepopulate
#endif

// Make sure F3 in a new tab works if Find has previous string to search for.
IN_PROC_BROWSER_TEST_F(FindInPageControllerTest,
                       FindNextInNewTabUsesPrepopulate) {
  HTTPTestServer* server = StartHTTPServer();
  ASSERT_TRUE(server);

  // First we navigate to any page.
  GURL url = server->TestServerPage(kSimplePage);
  ui_test_utils::NavigateToURL(browser(), url);

  // Search for 'no_match'. No matches should be found.
  int ordinal = 0;
  TabContents* tab = browser()->GetSelectedTabContents();
  EXPECT_EQ(0, FindInPageWchar(tab, L"no_match",
                               kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(0, ordinal);

  // Open another tab (tab B).
  browser()->NewTab();
  ui_test_utils::NavigateToURL(browser(), url);

  // Simulate what happens when you press F3 for FindNext. We should get a
  // response here (a hang means search was aborted).
  EXPECT_EQ(0, ui_test_utils::FindInPage(tab, string16(),
                                         kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(0, ordinal);

  // Open another tab (tab C).
  browser()->NewTab();
  ui_test_utils::NavigateToURL(browser(), url);

  // Simulate what happens when you press F3 for FindNext. We should get a
  // response here (a hang means search was aborted).
  EXPECT_EQ(0, ui_test_utils::FindInPage(tab, string16(),
                                         kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(0, ordinal);
}

#if defined(TOOLKIT_VIEWS)
// Make sure Find box grabs the Esc accelerator and restores it again.
#if defined(OS_LINUX)
// TODO(oshima): On Gtk/Linux, a focus out event is asynchronous and
// hiding a find bar does not immediately update the target
// accelerator. The last condition fails in most cases due to this
// behavior. See http://crbug.com/26870.
IN_PROC_BROWSER_TEST_F(FindInPageControllerTest,
                       DISABLED_AcceleratorRestoring) {
#else
  IN_PROC_BROWSER_TEST_F(FindInPageControllerTest, AcceleratorRestoring) {
#endif
  HTTPTestServer* server = StartHTTPServer();
  ASSERT_TRUE(server);

  // First we navigate to any page.
  GURL url = server->TestServerPage(kSimplePage);
  ui_test_utils::NavigateToURL(browser(), url);

  views::FocusManager* focus_manager =
      views::FocusManager::GetFocusManagerForNativeWindow(
          browser()->window()->GetNativeHandle());

  // See where Escape is registered.
  views::Accelerator escape(base::VKEY_ESCAPE, false, false, false);
  views::AcceleratorTarget* old_target =
      focus_manager->GetCurrentTargetForAccelerator(escape);
  EXPECT_TRUE(old_target != NULL);

  browser()->ShowFindBar();

  // Our Find bar should be the new target.
  views::AcceleratorTarget* new_target =
      focus_manager->GetCurrentTargetForAccelerator(escape);

  EXPECT_TRUE(new_target != NULL);
  EXPECT_NE(new_target, old_target);

  // Close the Find box.
  browser()->GetFindBarController()->EndFindSession(
      FindBarController::kKeepSelection);

  // The accelerator for Escape should be back to what it was before.
  EXPECT_EQ(old_target,
            focus_manager->GetCurrentTargetForAccelerator(escape));
}
#endif  // TOOLKIT_VIEWS

// Make sure Find box does not become UI-inactive when no text is in the box as
// we switch to a tab contents with an empty find string. See issue 13570.
IN_PROC_BROWSER_TEST_F(FindInPageControllerTest, StayActive) {
  HTTPTestServer* server = StartHTTPServer();
  ASSERT_TRUE(server);

  // First we navigate to any page.
  GURL url = server->TestServerPage(kSimplePage);
  ui_test_utils::NavigateToURL(browser(), url);

  browser()->ShowFindBar();

  // Simulate a user clearing the search string. Ideally, we should be
  // simulating keypresses here for searching for something and pressing
  // backspace, but that's been proven flaky in the past, so we go straight to
  // tab_contents.
  TabContents* tab_contents = browser()->GetSelectedTabContents();
  // Stop the (non-existing) find operation, and clear the selection (which
  // signals the UI is still active).
  tab_contents->StopFinding(FindBarController::kClearSelection);
  // Make sure the Find UI flag hasn't been cleared, it must be so that the UI
  // still responds to browser window resizing.
  ASSERT_TRUE(tab_contents->find_ui_active());
}

// Make sure F3 works after you FindNext a couple of times and end the Find
// session. See issue http://crbug.com/28306.
IN_PROC_BROWSER_TEST_F(FindInPageControllerTest, RestartSearchFromF3) {
  HTTPTestServer* server = StartHTTPServer();
  ASSERT_TRUE(server);

  // First we navigate to a simple page.
  GURL url = server->TestServerPage(kSimple);
  ui_test_utils::NavigateToURL(browser(), url);

  // Search for 'page'. Should have 1 match.
  int ordinal = 0;
  TabContents* tab = browser()->GetSelectedTabContents();
  EXPECT_EQ(1, FindInPageWchar(tab, L"page", kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(1, ordinal);

  // Simulate what happens when you press F3 for FindNext. Still should show
  // one match. This cleared the pre-populate string at one point (see bug).
  EXPECT_EQ(1, ui_test_utils::FindInPage(tab, string16(),
                                         kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(1, ordinal);

  // End the Find session, thereby making the next F3 start afresh.
  browser()->GetFindBarController()->EndFindSession(
      FindBarController::kKeepSelection);

  // Simulate F3 while Find box is closed. Should have 1 match.
  EXPECT_EQ(1, FindInPageWchar(tab, L"", kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(1, ordinal);
}

// When re-opening the find bar with F3, the find bar should be re-populated
// with the last search from the same tab rather than the last overall search.
// http://crbug.com/30006
IN_PROC_BROWSER_TEST_F(FindInPageControllerTest, PreferPreviousSearch) {
  HTTPTestServer* server = StartHTTPServer();
  ASSERT_TRUE(server);

  // First we navigate to any page.
  GURL url = server->TestServerPage(kSimplePage);
  ui_test_utils::NavigateToURL(browser(), url);

  // Find "Default".
  int ordinal = 0;
  TabContents* tab1 = browser()->GetSelectedTabContents();
  EXPECT_EQ(1, FindInPageWchar(tab1, L"Default", kFwd, kIgnoreCase, &ordinal));

  // Create a second tab.
  browser()->AddTabWithURL(url, GURL(), PageTransition::TYPED, -1,
                           TabStripModel::ADD_SELECTED, NULL, std::string());
  browser()->SelectTabContentsAt(1, false);
  TabContents* tab2 = browser()->GetSelectedTabContents();
  EXPECT_NE(tab1, tab2);

  // Find "given".
  FindInPageWchar(tab2, L"given", kFwd, kIgnoreCase, &ordinal);

  // Switch back to first tab.
  browser()->SelectTabContentsAt(0, false);
  browser()->GetFindBarController()->EndFindSession(
      FindBarController::kKeepSelection);
  // Simulate F3.
  ui_test_utils::FindInPage(tab1, string16(), kFwd, kIgnoreCase, &ordinal);
  EXPECT_EQ(tab1->find_text(), WideToUTF16(L"Default"));
}

// This tests that whenever you close and reopen the Find bar, it should show
// the last search entered in that tab. http://crbug.com/40121.
IN_PROC_BROWSER_TEST_F(FindInPageControllerTest, PrepopulateSameTab) {
#if defined(OS_MACOSX)
  // FindInPage on Mac doesn't use prepopulated values. Search there is global.
  return;
#endif

  HTTPTestServer* server = StartHTTPServer();
  ASSERT_TRUE(server);

  // First we navigate to any page.
  GURL url = server->TestServerPage(kSimple);
  ui_test_utils::NavigateToURL(browser(), url);

  // Search for the word "page".
  int ordinal = 0;
  TabContents* tab1 = browser()->GetSelectedTabContents();
  EXPECT_EQ(1, FindInPageWchar(tab1, L"page", kFwd, kIgnoreCase, &ordinal));

  // Open the Find box.
  EnsureFindBoxOpen();

  EXPECT_EQ(ASCIIToUTF16("page"), GetFindBarText());

  // Close the Find box.
  browser()->GetFindBarController()->EndFindSession(
      FindBarController::kKeepSelection);

  // Open the Find box again.
  EnsureFindBoxOpen();

  // After the Find box has been reopened, it should have been prepopulated with
  // the word "page" again.
  EXPECT_EQ(ASCIIToUTF16("page"), GetFindBarText());
}

// This tests that whenever you open Find in a new tab it should prepopulate
// with a previous search term (in any tab), if a search has not been issued in
// this tab before.
IN_PROC_BROWSER_TEST_F(FindInPageControllerTest, PrepopulateInNewTab) {
#if defined(OS_MACOSX)
  // FindInPage on Mac doesn't use prepopulated values. Search there is global.
  return;
#endif

  HTTPTestServer* server = StartHTTPServer();
  ASSERT_TRUE(server);

  // First we navigate to any page.
  GURL url = server->TestServerPage(kSimple);
  ui_test_utils::NavigateToURL(browser(), url);

  // Search for the word "page".
  int ordinal = 0;
  TabContents* tab1 = browser()->GetSelectedTabContents();
  EXPECT_EQ(1, FindInPageWchar(tab1, L"page", kFwd, kIgnoreCase, &ordinal));

  // Now create a second tab and load the same page.
  browser()->AddTabWithURL(url, GURL(), PageTransition::TYPED, -1,
                           TabStripModel::ADD_SELECTED, NULL, std::string());
  browser()->SelectTabContentsAt(1, false);
  TabContents* tab2 = browser()->GetSelectedTabContents();
  EXPECT_NE(tab1, tab2);

  // Open the Find box.
  EnsureFindBoxOpen();

  // The new tab should have "page" prepopulated, since that was the last search
  // in the first tab.
  EXPECT_EQ(ASCIIToUTF16("page"), GetFindBarText());
}

// This makes sure that we can search for A in tabA, then for B in tabB and
// when we come back to tabA we should still see A (because that was the last
// search in that tab).
IN_PROC_BROWSER_TEST_F(FindInPageControllerTest, PrepopulatePreserveLast) {
#if defined(OS_MACOSX)
  // FindInPage on Mac doesn't use prepopulated values. Search there is global.
  return;
#endif

  HTTPTestServer* server = StartHTTPServer();
  ASSERT_TRUE(server);

  // First we navigate to any page.
  GURL url = server->TestServerPage(kSimple);
  ui_test_utils::NavigateToURL(browser(), url);

  // Search for the word "page".
  int ordinal = 0;
  TabContents* tab1 = browser()->GetSelectedTabContents();
  EXPECT_EQ(1, FindInPageWchar(tab1, L"page", kFwd, kIgnoreCase, &ordinal));

  // Open the Find box.
  EnsureFindBoxOpen();

  EXPECT_EQ(ASCIIToUTF16("page"), GetFindBarText());

  // Close the Find box.
  browser()->GetFindBarController()->EndFindSession(
      FindBarController::kKeepSelection);

  // Now create a second tab and load the same page.
  browser()->AddTabWithURL(url, GURL(), PageTransition::TYPED, -1,
                           TabStripModel::ADD_SELECTED, NULL, std::string());
  browser()->SelectTabContentsAt(1, false);
  TabContents* tab2 = browser()->GetSelectedTabContents();
  EXPECT_NE(tab1, tab2);

  // Search for the word "text".
  FindInPageWchar(tab2, L"text", kFwd, kIgnoreCase, &ordinal);

  // Go back to the first tab and make sure we have NOT switched the prepopulate
  // text to "text".
  browser()->SelectTabContentsAt(0, false);

  // Open the Find box.
  EnsureFindBoxOpen();

  // After the Find box has been reopened, it should have been prepopulated with
  // the word "page" again, since that was the last search in that tab.
  EXPECT_EQ(ASCIIToUTF16("page"), GetFindBarText());

  // Close the Find box.
  browser()->GetFindBarController()->EndFindSession(
      FindBarController::kKeepSelection);

  // Re-open the Find box.
  // This is a special case: previous search in TabContents used to get cleared
  // if you opened and closed the FindBox, which would cause the global
  // prepopulate value to show instead of last search in this tab.
  EnsureFindBoxOpen();

  // After the Find box has been reopened, it should have been prepopulated with
  // the word "page" again, since that was the last search in that tab.
  EXPECT_EQ(ASCIIToUTF16("page"), GetFindBarText());
}

// TODO(rohitrao): Searching in incognito tabs does not work in browser tests in
// linux views.  Investigate and fix.  http://crbug.com/40948
#if defined(OS_LINUX) && defined(TOOLKIT_VIEWS)
#define MAYBE_NoIncognitoPrepopulate DISABLED_NoIncognitoPrepopulate
#elif defined (OS_WIN)
// On windows, this test is flaky. http://crbug.com/40948
#define MAYBE_NoIncognitoPrepopulate FLAKY_NoIncognitoPrepopulate
#else
#define MAYBE_NoIncognitoPrepopulate NoIncognitoPrepopulate
#endif

// This tests that search terms entered into an incognito find bar are not used
// as prepopulate terms for non-incognito windows.
IN_PROC_BROWSER_TEST_F(FindInPageControllerTest, MAYBE_NoIncognitoPrepopulate) {
#if defined(OS_MACOSX)
  // FindInPage on Mac doesn't use prepopulated values. Search there is global.
  return;
#endif

  HTTPTestServer* server = StartHTTPServer();
  ASSERT_TRUE(server);

  // First we navigate to the "simple" test page.
  GURL url = server->TestServerPage(kSimple);
  ui_test_utils::NavigateToURL(browser(), url);

  // Search for the word "page" in the normal browser tab.
  int ordinal = 0;
  TabContents* tab1 = browser()->GetSelectedTabContents();
  EXPECT_EQ(1, FindInPageWchar(tab1, L"page", kFwd, kIgnoreCase, &ordinal));

  // Open the Find box.
  EnsureFindBoxOpenForBrowser(browser());
  EXPECT_EQ(ASCIIToUTF16("page"), GetFindBarTextForBrowser(browser()));

  // Close the Find box.
  browser()->GetFindBarController()->EndFindSession(
      FindBarController::kKeepSelection);

  // Open a new incognito window and navigate to the same page.
  Profile* incognito_profile = browser()->profile()->GetOffTheRecordProfile();
  Browser* incognito_browser = Browser::Create(incognito_profile);
  incognito_browser->AddTabWithURL(url, GURL(), PageTransition::START_PAGE, -1,
                                   TabStripModel::ADD_SELECTED, NULL,
                                   std::string());
  ui_test_utils::WaitForNavigation(
      &incognito_browser->GetSelectedTabContents()->controller());
  incognito_browser->window()->Show();

  // Open the find box and make sure that it is prepopulated with "page".
  EnsureFindBoxOpenForBrowser(incognito_browser);
  EXPECT_EQ(ASCIIToUTF16("page"), GetFindBarTextForBrowser(incognito_browser));

  // Search for the word "text" in the incognito tab.
  TabContents* incognito_tab = incognito_browser->GetSelectedTabContents();
  EXPECT_EQ(1, FindInPageWchar(incognito_tab, L"text",
                               kFwd, kIgnoreCase, &ordinal));
  EXPECT_EQ(ASCIIToUTF16("text"), GetFindBarTextForBrowser(incognito_browser));

  // Close the Find box.
  incognito_browser->GetFindBarController()->EndFindSession(
      FindBarController::kKeepSelection);

  // Now open a new tab in the original (non-incognito) browser.
  browser()->AddTabWithURL(url, GURL(), PageTransition::TYPED, -1,
                           TabStripModel::ADD_SELECTED, NULL, std::string());
  browser()->SelectTabContentsAt(1, false);
  TabContents* tab2 = browser()->GetSelectedTabContents();
  EXPECT_NE(tab1, tab2);

  // Open the Find box and make sure it is prepopulated with the search term
  // from the original browser, not the search term from the incognito window.
  EnsureFindBoxOpenForBrowser(browser());
  EXPECT_EQ(ASCIIToUTF16("page"), GetFindBarTextForBrowser(browser()));
}

// See http://crbug.com/45594. On Windows, it crashes sometimes.
#if defined(OS_WIN)
#define MAYBE_ActivateLinkNavigatesPage DISABLED_ActivateLinkNavigatesPage
#else
#define MAYBE_ActivateLinkNavigatesPage ActivateLinkNavigatesPage
#endif
// This makes sure that dismissing the find bar with kActivateSelection works.
IN_PROC_BROWSER_TEST_F(FindInPageControllerTest,
                       MAYBE_ActivateLinkNavigatesPage) {
  HTTPTestServer* server = StartHTTPServer();
  ASSERT_TRUE(server);

  // First we navigate to our test content.
  GURL url = server->TestServerPage(kLinkPage);
  ui_test_utils::NavigateToURL(browser(), url);

  TabContents* tab = browser()->GetSelectedTabContents();
  int ordinal = 0;
  FindInPageWchar(tab, L"link", kFwd, kIgnoreCase, &ordinal);
  EXPECT_EQ(ordinal, 1);

  // End the find session, click on the link.
  tab->StopFinding(FindBarController::kActivateSelection);
  EXPECT_TRUE(ui_test_utils::WaitForNavigationInCurrentTab(browser()));
}
