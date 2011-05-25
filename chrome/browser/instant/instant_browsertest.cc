// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/stringprintf.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/autocomplete/autocomplete_edit_view.h"
#include "chrome/browser/browser_list.h"
#include "chrome/browser/browser_window.h"
#include "chrome/browser/instant/instant_controller.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/renderer_host/render_view_host.h"
#include "chrome/browser/renderer_host/render_widget_host_view.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_model.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/omnibox/location_bar.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/in_process_browser_test.h"
#include "chrome/test/ui_test_utils.h"

class InstantTest : public InProcessBrowserTest {
 public:
  InstantTest()
      : location_bar_(NULL),
        preview_(NULL) {
    set_show_window(true);
    EnableDOMAutomation();
  }

  void SetupInstantProvider(const std::string& page) {
    TemplateURLModel* model = browser()->profile()->GetTemplateURLModel();
    ASSERT_TRUE(model);

    if (!model->loaded()) {
      model->Load();
      ui_test_utils::WaitForNotification(
          NotificationType::TEMPLATE_URL_MODEL_LOADED);
    }

    ASSERT_TRUE(model->loaded());

    // TemplateURLModel takes ownership of this.
    TemplateURL* template_url = new TemplateURL();

    std::string url = StringPrintf(
        "http://%s:%d/files/instant/%s?q={searchTerms}",
        test_server()->host_port_pair().host().c_str(),
        test_server()->host_port_pair().port(),
        page.c_str());
    template_url->SetURL(url, 0, 0);
    template_url->SetInstantURL(url, 0, 0);
    template_url->set_keyword(UTF8ToWide("foo"));
    template_url->set_short_name(UTF8ToWide("foo"));

    model->Add(template_url);
    model->SetDefaultSearchProvider(template_url);
  }

  void FindLocationBar() {
    if (location_bar_)
      return;
    location_bar_ = browser()->window()->GetLocationBar();
    ASSERT_TRUE(location_bar_);
  }

  TabContentsWrapper* GetPendingPreviewContents() {
    return browser()->instant()->GetPendingPreviewContents();
  }

  // Type a character to get instant to trigger.
  void SetupLocationBar() {
    FindLocationBar();
    location_bar_->location_entry()->SetUserText(L"a");
  }

  // Waits for preview to be shown.
  void WaitForPreviewToNavigate(bool use_current) {
    InstantController* instant = browser()->instant();
    ASSERT_TRUE(instant);
    TabContentsWrapper* tab = use_current ?
        instant->GetPreviewContents() : GetPendingPreviewContents();
    ASSERT_TRUE(tab);
    preview_ = tab->tab_contents();
    ASSERT_TRUE(preview_);
    ui_test_utils::WaitForNavigation(&preview_->controller());
  }

  // Wait for instant to load and ensure it is in the state we expect.
  void SetupPreview() {
    // Wait for the preview to navigate.
    WaitForPreviewToNavigate(true);

    ASSERT_TRUE(browser()->instant()->IsShowingInstant());
    ASSERT_FALSE(browser()->instant()->is_displayable());
    ASSERT_TRUE(browser()->instant()->is_active());

    // When the page loads, the initial searchBox values are set and only a
    // resize will have been sent.
    ASSERT_EQ("true 0 0 0 1 a false a false",
        GetSearchStateAsString(preview_));
  }

  void SetLocationBarText(const std::wstring& text) {
    ASSERT_NO_FATAL_FAILURE(FindLocationBar());
    location_bar_->location_entry()->SetUserText(text);
    ui_test_utils::WaitForNotification(
        NotificationType::INSTANT_CONTROLLER_SHOWN);
  }

  void SendKey(app::KeyboardCode key) {
    ASSERT_TRUE(ui_test_utils::SendKeyPressSync(
        browser(), key, false, false, false, false));
  }

  bool GetStringFromJavascript(TabContents* tab_contents,
                               const std::string& function,
                               std::string* result) {
    std::string script = StringPrintf(
        "window.domAutomationController.send(%s)", function.c_str());
    return ui_test_utils::ExecuteJavaScriptAndExtractString(
        tab_contents->render_view_host(),
        std::wstring(), UTF8ToWide(script), result);
  }

  bool GetIntFromJavascript(TabContents* tab_contents,
                            const std::string& function,
                            int* result) {
    std::string script = StringPrintf(
        "window.domAutomationController.send(%s)", function.c_str());
    return ui_test_utils::ExecuteJavaScriptAndExtractInt(
        tab_contents->render_view_host(),
        std::wstring(), UTF8ToWide(script), result);
  }

  bool GetBoolFromJavascript(TabContents* tab_contents,
                             const std::string& function,
                             bool* result) {
    std::string script = StringPrintf(
        "window.domAutomationController.send(%s)", function.c_str());
    return ui_test_utils::ExecuteJavaScriptAndExtractBool(
        tab_contents->render_view_host(),
        std::wstring(), UTF8ToWide(script), result);
  }

  // Returns the state of the search box as a string. This consists of the
  // following:
  // window.chrome.sv
  // window.onsubmitcalls
  // window.oncancelcalls
  // window.onchangecalls
  // window.onresizecalls
  // window.beforeLoadSearchBox.value
  // window.beforeLoadSearchBox.verbatim
  // window.chrome.searchBox.value
  // window.chrome.searchBox.verbatim
  // If determining any of the values fails, the value is 'fail'.
  std::string GetSearchStateAsString(TabContents* tab_contents) {
    bool sv = false;
    int onsubmitcalls = 0;
    int oncancelcalls = 0;
    int onchangecalls = 0;
    int onresizecalls = 0;
    std::string before_load_value;
    bool before_load_verbatim = false;
    std::string value;
    bool verbatim = false;

    if (!GetBoolFromJavascript(tab_contents, "window.chrome.sv", &sv) ||
        !GetIntFromJavascript(tab_contents, "window.onsubmitcalls",
                              &onsubmitcalls) ||
        !GetIntFromJavascript(tab_contents, "window.oncancelcalls",
                              &oncancelcalls) ||
        !GetIntFromJavascript(tab_contents, "window.onchangecalls",
                              &onchangecalls) ||
        !GetIntFromJavascript(tab_contents, "window.onresizecalls",
                              &onresizecalls) ||
        !GetStringFromJavascript(
            tab_contents, "window.beforeLoadSearchBox.value",
            &before_load_value) ||
        !GetBoolFromJavascript(
            tab_contents, "window.beforeLoadSearchBox.verbatim",
            &before_load_verbatim) ||
        !GetStringFromJavascript(tab_contents, "window.chrome.searchBox.value",
                                 &value) ||
        !GetBoolFromJavascript(tab_contents, "window.chrome.searchBox.verbatim",
                               &verbatim)) {
      return "fail";
    }

    return StringPrintf("%s %d %d %d %d %s %s %s %s",
                        sv ? "true" : "false",
                        onsubmitcalls,
                        oncancelcalls,
                        onchangecalls,
                        onresizecalls,
                        before_load_value.c_str(),
                        before_load_verbatim ? "true" : "false",
                        value.c_str(),
                        verbatim ? "true" : "false");
  }

  void CheckStringValueFromJavascript(
      const std::string& expected,
      const std::string& function,
      TabContents* tab_contents) {
    std::string result;
    ASSERT_TRUE(GetStringFromJavascript(tab_contents, function, &result));
    ASSERT_EQ(expected, result);
  }

  void CheckBoolValueFromJavascript(
      bool expected,
      const std::string& function,
      TabContents* tab_contents) {
    bool result;
    ASSERT_TRUE(GetBoolFromJavascript(tab_contents, function, &result));
    ASSERT_EQ(expected, result);
  }

  void CheckIntValueFromJavascript(
      int expected,
      const std::string& function,
      TabContents* tab_contents) {
    int result;
    ASSERT_TRUE(GetIntFromJavascript(tab_contents, function, &result));
    ASSERT_EQ(expected, result);
  }

  // Sends a message to the renderer and waits for the response to come back to
  // the browser.
  void WaitForMessageToBeProcessedByRenderer(TabContentsWrapper* tab) {
    ASSERT_NO_FATAL_FAILURE(
        CheckBoolValueFromJavascript(true, "true", tab->tab_contents()));
  }

 protected:
  virtual void SetUpCommandLine(CommandLine* command_line) {
    command_line->AppendSwitch(switches::kEnablePredictiveInstant);
  }

  LocationBar* location_bar_;
  TabContents* preview_;
};

// TODO(tonyg): Add the following tests:
// 1. Test that setSuggestions() works.
// 2. Test that the search box API is not populated for pages other than the
//    default search provider.
// 3. Test resize events.

// Verify that the onchange event is dispatched upon typing in the box.
IN_PROC_BROWSER_TEST_F(InstantTest, OnChangeEvent) {
  ASSERT_TRUE(test_server()->Start());
  ASSERT_NO_FATAL_FAILURE(SetupInstantProvider("search.html"));
  ASSERT_NO_FATAL_FAILURE(SetupLocationBar());
  ASSERT_NO_FATAL_FAILURE(SetupPreview());

  ASSERT_NO_FATAL_FAILURE(SetLocationBarText(L"abc"));

  // Check that the value is reflected and onchange is called.
  EXPECT_EQ("true 0 0 1 1 a false abc false",
      GetSearchStateAsString(preview_));
}

// Verify instant preview is shown correctly for a non-search query.
IN_PROC_BROWSER_TEST_F(InstantTest, ShowPreviewNonSearch) {
  ASSERT_TRUE(test_server()->Start());
  GURL url(test_server()->GetURL("files/instant/empty.html"));
  ASSERT_NO_FATAL_FAILURE(SetLocationBarText(UTF8ToWide(url.spec())));
  // The preview should be active and showing.
  ASSERT_TRUE(browser()->instant()->is_active());
  ASSERT_TRUE(browser()->instant()->is_displayable());
  ASSERT_TRUE(browser()->instant()->IsCurrent());
  ASSERT_TRUE(browser()->instant()->GetPreviewContents());
  RenderWidgetHostView* rwhv =
      browser()->instant()->GetPreviewContents()->tab_contents()->
      GetRenderWidgetHostView();
  ASSERT_TRUE(rwhv);
  ASSERT_TRUE(rwhv->IsShowing());
}

// Transition from non-search to search and make sure everything is shown
// correctly.
IN_PROC_BROWSER_TEST_F(InstantTest, NonSearchToSearch) {
  ASSERT_TRUE(test_server()->Start());
  GURL url(test_server()->GetURL("files/instant/empty.html"));
  ASSERT_NO_FATAL_FAILURE(SetLocationBarText(UTF8ToWide(url.spec())));
  // The preview should be active and showing.
  ASSERT_TRUE(browser()->instant()->is_active());
  ASSERT_TRUE(browser()->instant()->is_displayable());
  TabContentsWrapper* initial_tab = browser()->instant()->GetPreviewContents();
  ASSERT_TRUE(initial_tab);
  RenderWidgetHostView* rwhv =
      initial_tab->tab_contents()->GetRenderWidgetHostView();
  ASSERT_TRUE(rwhv);
  ASSERT_TRUE(rwhv->IsShowing());

  // Now type in some search text.
  ASSERT_NO_FATAL_FAILURE(SetupInstantProvider("search.html"));
  location_bar_->location_entry()->SetUserText(L"abc");

  // Wait for the preview to navigate.
  ASSERT_NO_FATAL_FAILURE(WaitForPreviewToNavigate(false));

  // The controller is still determining if the provider really supports
  // instant. As a result the tabcontents should not have changed.
  TabContentsWrapper* current_tab = browser()->instant()->GetPreviewContents();
  ASSERT_EQ(current_tab, initial_tab);
  // The preview should still be showing.
  rwhv = current_tab->tab_contents()->GetRenderWidgetHostView();
  ASSERT_TRUE(rwhv);
  ASSERT_TRUE(rwhv->IsShowing());

  // Use MightSupportInstant as the controller is still determining if the
  // page supports instant and hasn't actually commited yet.
  EXPECT_TRUE(browser()->instant()->MightSupportInstant());

  // Instant should still be active.
  EXPECT_TRUE(browser()->instant()->is_active());
  EXPECT_TRUE(browser()->instant()->is_displayable());

  // Because we're waiting on the page, instant isn't current.
  ASSERT_FALSE(browser()->instant()->IsCurrent());

  // Bounce a message to the renderer so that we know the instant has gotten a
  // response back from the renderer as to whether the page supports instant.
  ASSERT_NO_FATAL_FAILURE(
      WaitForMessageToBeProcessedByRenderer(GetPendingPreviewContents()));

  // Reset the user text so that the page is told the text changed. We should be
  // able to nuke this once 66104 is fixed.
  location_bar_->location_entry()->SetUserText(L"abcd");

  // Wait for the renderer to process it.
  ASSERT_NO_FATAL_FAILURE(
      WaitForMessageToBeProcessedByRenderer(GetPendingPreviewContents()));

  // We should have gotten a response back from the renderer that resulted in
  // committing.
  ASSERT_FALSE(GetPendingPreviewContents());
  ASSERT_TRUE(browser()->instant()->is_active());
  ASSERT_TRUE(browser()->instant()->is_displayable());
  TabContentsWrapper* new_tab = browser()->instant()->GetPreviewContents();
  ASSERT_TRUE(new_tab);
  ASSERT_NE(new_tab, initial_tab);
  RenderWidgetHostView* new_rwhv =
      new_tab->tab_contents()->GetRenderWidgetHostView();
  ASSERT_TRUE(new_rwhv);
  ASSERT_NE(new_rwhv, rwhv);
  ASSERT_TRUE(new_rwhv->IsShowing());
}

// Makes sure that if the server doesn't support the instant API we don't show
// anything.
IN_PROC_BROWSER_TEST_F(InstantTest, SearchServerDoesntSupportInstant) {
  ASSERT_TRUE(test_server()->Start());
  ASSERT_NO_FATAL_FAILURE(SetupInstantProvider("empty.html"));
  ASSERT_NO_FATAL_FAILURE(FindLocationBar());
  location_bar_->location_entry()->SetUserText(L"a");
  ASSERT_TRUE(browser()->instant());
  // Because we typed in a search string we should think we're showing instant
  // results.
  EXPECT_TRUE(browser()->instant()->IsShowingInstant());
  // But because we're waiting to determine if the page really supports instant
  // we shouldn't be showing the preview.
  EXPECT_FALSE(browser()->instant()->is_displayable());
  // But instant should still be active.
  EXPECT_TRUE(browser()->instant()->is_active());

  // When the response comes back that the page doesn't support instant the tab
  // should be closed.
  ui_test_utils::WaitForNotification(NotificationType::TAB_CLOSED);
  EXPECT_FALSE(browser()->instant()->IsShowingInstant());
  EXPECT_FALSE(browser()->instant()->is_displayable());
  EXPECT_TRUE(browser()->instant()->is_active());
  EXPECT_FALSE(browser()->instant()->IsCurrent());
}

// Verifies transitioning from loading a non-search string to a search string
// with the provider not supporting instant works (meaning we don't display
// anything).
IN_PROC_BROWSER_TEST_F(InstantTest, NonSearchToSearchDoesntSupportInstant) {
  ASSERT_TRUE(test_server()->Start());
  ASSERT_NO_FATAL_FAILURE(SetupInstantProvider("empty.html"));
  GURL url(test_server()->GetURL("files/instant/empty.html"));
  ASSERT_NO_FATAL_FAILURE(SetLocationBarText(UTF8ToWide(url.spec())));
  // The preview should be active and showing.
  ASSERT_TRUE(browser()->instant()->is_displayable());
  ASSERT_TRUE(browser()->instant()->is_active());
  TabContentsWrapper* initial_tab = browser()->instant()->GetPreviewContents();
  ASSERT_TRUE(initial_tab);
  RenderWidgetHostView* rwhv =
      initial_tab->tab_contents()->GetRenderWidgetHostView();
  ASSERT_TRUE(rwhv);
  ASSERT_TRUE(rwhv->IsShowing());

  // Now type in some search text.
  location_bar_->location_entry()->SetUserText(L"a");

  // Instant should still be live.
  ASSERT_TRUE(browser()->instant()->is_displayable());
  ASSERT_TRUE(browser()->instant()->is_active());
  // Because we typed in a search string we should think we're showing instant
  // results.
  EXPECT_TRUE(browser()->instant()->MightSupportInstant());
  // Instant should not be current (it's still loading).
  EXPECT_FALSE(browser()->instant()->IsCurrent());

  // When the response comes back that the page doesn't support instant the tab
  // should be closed.
  ui_test_utils::WaitForNotification(NotificationType::TAB_CLOSED);
  EXPECT_FALSE(browser()->instant()->IsShowingInstant());
  EXPECT_FALSE(browser()->instant()->is_displayable());
  // But because the omnibox is still open, instant should be active.
  ASSERT_TRUE(browser()->instant()->is_active());
}

// Verifies the page was told a non-zero height.
IN_PROC_BROWSER_TEST_F(InstantTest, ValidHeight) {
  ASSERT_TRUE(test_server()->Start());
  ASSERT_NO_FATAL_FAILURE(SetupInstantProvider("search.html"));
  ASSERT_NO_FATAL_FAILURE(SetupLocationBar());
  ASSERT_NO_FATAL_FAILURE(SetupPreview());

  ASSERT_NO_FATAL_FAILURE(SetLocationBarText(L"abc"));

  int height;

  // searchBox height is not yet set during initial load.
  ASSERT_TRUE(GetIntFromJavascript(preview_,
      "window.beforeLoadSearchBox.height",
      &height));
  EXPECT_EQ(0, height);

  // searchBox height is available by the time the page loads.
  ASSERT_TRUE(GetIntFromJavascript(preview_,
      "window.chrome.searchBox.height",
      &height));
  EXPECT_GT(height, 0);
}

// Verifies that if the server returns a 403 we don't show the preview and
// query the host again.
IN_PROC_BROWSER_TEST_F(InstantTest, HideOn403) {
  ASSERT_TRUE(test_server()->Start());
  GURL url(test_server()->GetURL("files/instant/403.html"));
  ASSERT_NO_FATAL_FAILURE(FindLocationBar());
  location_bar_->location_entry()->SetUserText(UTF8ToWide(url.spec()));
  // The preview shouldn't be showing, but it should be loading.
  ASSERT_TRUE(browser()->instant()->GetPreviewContents());
  ASSERT_TRUE(browser()->instant()->is_active());
  ASSERT_FALSE(browser()->instant()->is_displayable());

  // When instant sees the 403, it should close the tab.
  ui_test_utils::WaitForNotification(NotificationType::TAB_CLOSED);
  ASSERT_FALSE(browser()->instant()->GetPreviewContents());
  ASSERT_TRUE(browser()->instant()->is_active());
  ASSERT_FALSE(browser()->instant()->is_displayable());

  // Try loading another url on the server. Instant shouldn't create a new tab
  // as the server returned 403.
  GURL url2(test_server()->GetURL("files/instant/empty.html"));
  location_bar_->location_entry()->SetUserText(UTF8ToWide(url2.spec()));
  ASSERT_FALSE(browser()->instant()->GetPreviewContents());
  ASSERT_TRUE(browser()->instant()->is_active());
  ASSERT_FALSE(browser()->instant()->is_displayable());
}

// Verify that the onsubmit event is dispatched upon pressing enter.
IN_PROC_BROWSER_TEST_F(InstantTest, OnSubmitEvent) {
  ASSERT_TRUE(test_server()->Start());
  ASSERT_NO_FATAL_FAILURE(SetupInstantProvider("search.html"));

  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
  ASSERT_NO_FATAL_FAILURE(SetupLocationBar());
  ASSERT_NO_FATAL_FAILURE(SetupPreview());

  ASSERT_NO_FATAL_FAILURE(SetLocationBarText(L"abc"));
  ASSERT_NO_FATAL_FAILURE(SendKey(app::VKEY_RETURN));

  // Check that the preview contents have been committed.
  ASSERT_FALSE(browser()->instant()->GetPreviewContents());
  ASSERT_FALSE(browser()->instant()->is_active());
  TabContents* contents = browser()->GetSelectedTabContents();
  ASSERT_TRUE(contents);

  // Check that the value is reflected and onsubmit is called.
  EXPECT_EQ("true 1 0 1 1 a false abc true",
      GetSearchStateAsString(preview_));
}

// Verify that the oncancel event is dispatched upon losing focus.
IN_PROC_BROWSER_TEST_F(InstantTest, OnCancelEvent) {
  ASSERT_TRUE(test_server()->Start());
  ASSERT_NO_FATAL_FAILURE(SetupInstantProvider("search.html"));

  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
  ASSERT_NO_FATAL_FAILURE(SetupLocationBar());
  ASSERT_NO_FATAL_FAILURE(SetupPreview());

  ASSERT_NO_FATAL_FAILURE(SetLocationBarText(L"abc"));
  ASSERT_NO_FATAL_FAILURE(ui_test_utils::ClickOnView(browser(),
                                                     VIEW_ID_TAB_CONTAINER));

  // Check that the preview contents have been committed.
  ASSERT_FALSE(browser()->instant()->GetPreviewContents());
  ASSERT_FALSE(browser()->instant()->is_active());
  TabContents* contents = browser()->GetSelectedTabContents();
  ASSERT_TRUE(contents);

  // Check that the value is reflected and oncancel is called.
  EXPECT_EQ("true 0 1 1 1 a false abc false",
      GetSearchStateAsString(preview_));
}

#if !defined(OS_MACOSX)
// Only passes on Mac.  http://crbug.com/66850
#define MAYBE_TabKey FAILS_TabKey
#else
#define MAYBE_TabKey TabKey
#endif
IN_PROC_BROWSER_TEST_F(InstantTest, MAYBE_TabKey) {
  ASSERT_TRUE(test_server()->Start());
  ASSERT_NO_FATAL_FAILURE(SetupInstantProvider("search.html"));

  ASSERT_TRUE(ui_test_utils::BringBrowserWindowToFront(browser()));
  ASSERT_NO_FATAL_FAILURE(SetupLocationBar());
  ASSERT_NO_FATAL_FAILURE(SetupPreview());

  ASSERT_NO_FATAL_FAILURE(SetLocationBarText(L"abc"));

  // Pressing tab to convert instant suggest into inline autocomplete.
  ASSERT_NO_FATAL_FAILURE(SendKey(app::VKEY_TAB));

  ASSERT_EQ(L"abcdef", location_bar_->location_entry()->GetText());

  EXPECT_EQ("true 0 0 2 2 a false abcdef false",
      GetSearchStateAsString(preview_));

  // Pressing tab again to accept the current instant preview.
  ASSERT_NO_FATAL_FAILURE(SendKey(app::VKEY_TAB));

  // Check that the preview contents have been committed.
  ASSERT_FALSE(browser()->instant()->GetPreviewContents());
  ASSERT_FALSE(browser()->instant()->is_active());
  TabContents* contents = browser()->GetSelectedTabContents();
  ASSERT_TRUE(contents);

  // Check that the value is reflected and onsubmit is called.
  EXPECT_EQ("true 1 0 2 2 a false abcdef true",
      GetSearchStateAsString(preview_));
}
