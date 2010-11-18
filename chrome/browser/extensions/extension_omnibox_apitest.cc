// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/string_util.h"
#include "base/stringprintf.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/autocomplete/autocomplete.h"
#include "chrome/browser/autocomplete/autocomplete_edit.h"
#include "chrome/browser/autocomplete/autocomplete_edit_view.h"
#include "chrome/browser/autocomplete/autocomplete_match.h"
#include "chrome/browser/autocomplete/autocomplete_popup_model.h"
#include "chrome/browser/browser.h"
#include "chrome/browser/browser_window.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/location_bar.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/search_engines/template_url.h"
#include "chrome/browser/search_engines/template_url_model.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/notification_type.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/ui_test_utils.h"

// Basic test is flaky on ChromeOS.
// http://crbug.com/52929
#if defined(OS_CHROMEOS)
#define MAYBE_Basic FLAKY_Basic
#else
#define MAYBE_Basic Basic
#endif

namespace {

std::wstring AutocompleteResultAsString(const AutocompleteResult& result) {
  std::wstring output(base::StringPrintf(L"{%d} ", result.size()));
  for (size_t i = 0; i < result.size(); ++i) {
    AutocompleteMatch match = result.match_at(i);
    std::wstring provider_name(ASCIIToWide(match.provider->name()));
    output.append(base::StringPrintf(L"[\"%ls\" by \"%ls\"] ",
                                     match.contents.c_str(),
                                     provider_name.c_str()));
  }
  return output;
}

}  // namespace

class OmniboxApiTest : public ExtensionApiTest {
 protected:
  LocationBar* GetLocationBar() const {
    return browser()->window()->GetLocationBar();
  }

  AutocompleteController* GetAutocompleteController() const {
    return GetLocationBar()->location_entry()->model()->popup_model()->
        autocomplete_controller();
  }

  void WaitForTemplateURLModelToLoad() {
    TemplateURLModel* model =
        browser()->profile()->GetTemplateURLModel();
    model->Load();
    if (!model->loaded()) {
      ui_test_utils::WaitForNotification(
          NotificationType::TEMPLATE_URL_MODEL_LOADED);
    }
  }

  void WaitForAutocompleteDone(AutocompleteController* controller) {
    while (!controller->done()) {
      ui_test_utils::WaitForNotification(
          NotificationType::AUTOCOMPLETE_CONTROLLER_RESULT_UPDATED);
    }
  }
};

IN_PROC_BROWSER_TEST_F(OmniboxApiTest, MAYBE_Basic) {
  CommandLine::ForCurrentProcess()->AppendSwitch(
      switches::kEnableExperimentalExtensionApis);

  ASSERT_TRUE(test_server()->Start());
  ASSERT_TRUE(RunExtensionTest("omnibox")) << message_;

  // The results depend on the TemplateURLModel being loaded. Make sure it is
  // loaded so that the autocomplete results are consistent.
  WaitForTemplateURLModelToLoad();

  LocationBar* location_bar = GetLocationBar();
  AutocompleteController* autocomplete_controller = GetAutocompleteController();

  // Test that our extension's keyword is suggested to us when we partially type
  // it.
  {
    autocomplete_controller->Start(L"keywor", std::wstring(),
                                   true, false, false);

    WaitForAutocompleteDone(autocomplete_controller);
    EXPECT_TRUE(autocomplete_controller->done());
    EXPECT_EQ(std::wstring(), location_bar->GetInputString());
    EXPECT_EQ(std::wstring(), location_bar->location_entry()->GetText());
    EXPECT_TRUE(location_bar->location_entry()->IsSelectAll());

    // First result should be to search for what was typed, second should be to
    // enter "extension keyword" mode.
    const AutocompleteResult& result = autocomplete_controller->result();
    ASSERT_EQ(2U, result.size()) << AutocompleteResultAsString(result);
    AutocompleteMatch match = result.match_at(0);
    EXPECT_EQ(AutocompleteMatch::SEARCH_WHAT_YOU_TYPED, match.type);
    EXPECT_FALSE(match.deletable);

    match = result.match_at(1);
    ASSERT_TRUE(match.template_url);
    EXPECT_TRUE(match.template_url->IsExtensionKeyword());
    EXPECT_EQ(L"keyword", match.template_url->keyword());
  }

  // Test that our extension can send suggestions back to us.
  {
    autocomplete_controller->Start(L"keyword suggestio", std::wstring(),
                                   true, false, false);

    WaitForAutocompleteDone(autocomplete_controller);
    EXPECT_TRUE(autocomplete_controller->done());

    // First result should be to invoke the keyword with what we typed, 2-4
    // should be to invoke with suggestions from the extension, and the last
    // should be to search for what we typed.
    const AutocompleteResult& result = autocomplete_controller->result();
    ASSERT_EQ(5U, result.size()) << AutocompleteResultAsString(result);

    ASSERT_TRUE(result.match_at(0).template_url);
    EXPECT_EQ(L"keyword suggestio", result.match_at(0).fill_into_edit);
    EXPECT_EQ(L"keyword suggestion1", result.match_at(1).fill_into_edit);
    EXPECT_EQ(L"keyword suggestion2", result.match_at(2).fill_into_edit);
    EXPECT_EQ(L"keyword suggestion3", result.match_at(3).fill_into_edit);

    std::wstring description = L"Description with style: <match> [dim], none";
    EXPECT_EQ(description, result.match_at(1).contents);
    ASSERT_EQ(5u, result.match_at(1).contents_class.size());
    EXPECT_EQ(0u,
              result.match_at(1).contents_class[0].offset);
    EXPECT_EQ(ACMatchClassification::NONE,
              result.match_at(1).contents_class[0].style);
    EXPECT_EQ(description.find('<'),
              result.match_at(1).contents_class[1].offset);
    EXPECT_EQ(ACMatchClassification::MATCH,
              result.match_at(1).contents_class[1].style);
    EXPECT_EQ(description.find('>'),
              result.match_at(1).contents_class[2].offset);
    EXPECT_EQ(ACMatchClassification::NONE,
              result.match_at(1).contents_class[2].style);
    EXPECT_EQ(description.find('['),
              result.match_at(1).contents_class[3].offset);
    EXPECT_EQ(ACMatchClassification::DIM,
              result.match_at(1).contents_class[3].style);
    EXPECT_EQ(description.find(']'),
              result.match_at(1).contents_class[4].offset);
    EXPECT_EQ(ACMatchClassification::NONE,
              result.match_at(1).contents_class[4].style);

    AutocompleteMatch match = result.match_at(4);
    EXPECT_EQ(AutocompleteMatch::SEARCH_WHAT_YOU_TYPED, match.type);
    EXPECT_FALSE(match.deletable);
  }

  {
    ResultCatcher catcher;
    autocomplete_controller->Start(L"keyword command", std::wstring(),
                                   true, false, false);
    location_bar->AcceptInput();
    EXPECT_TRUE(catcher.GetNextResult()) << catcher.message();
  }
}
