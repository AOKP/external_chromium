// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop.h"
#include "base/scoped_ptr.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/autocomplete/autocomplete.h"
#include "chrome/browser/autocomplete/autocomplete_match.h"
#include "chrome/common/notification_observer.h"
#include "chrome/common/notification_registrar.h"
#include "chrome/common/notification_service.h"
#include "testing/gtest/include/gtest/gtest.h"

// identifiers for known autocomplete providers
#define HISTORY_IDENTIFIER L"Chrome:History"
#define SEARCH_IDENTIFIER L"google.com/websearch/en"

static std::ostream& operator<<(std::ostream& os,
                                const AutocompleteResult::const_iterator& it) {
  return os << static_cast<const AutocompleteMatch*>(&(*it));
}

namespace {

const size_t num_results_per_provider = 3;

// Autocomplete provider that provides known results. Note that this is
// refcounted so that it can also be a task on the message loop.
class TestProvider : public AutocompleteProvider {
 public:
  TestProvider(int relevance, const std::wstring& prefix)
      : AutocompleteProvider(NULL, NULL, ""),
        relevance_(relevance),
        prefix_(prefix) {
  }

  virtual void Start(const AutocompleteInput& input,
                     bool minimal_changes);

  void set_listener(ACProviderListener* listener) {
    listener_ = listener;
  }

 private:
  ~TestProvider() {}

  void Run();

  void AddResults(int start_at, int num);

  int relevance_;
  const std::wstring prefix_;
};

void TestProvider::Start(const AutocompleteInput& input,
                         bool minimal_changes) {
  if (minimal_changes)
    return;

  matches_.clear();

  // Generate one result synchronously, the rest later.
  AddResults(0, 1);

  if (!input.synchronous_only()) {
    done_ = false;
    MessageLoop::current()->PostTask(FROM_HERE, NewRunnableMethod(
        this, &TestProvider::Run));
  }
}

void TestProvider::Run() {
  DCHECK_GT(num_results_per_provider, 0U);
  AddResults(1, num_results_per_provider);
  done_ = true;
  DCHECK(listener_);
  listener_->OnProviderUpdate(true);
}

void TestProvider::AddResults(int start_at, int num) {
  for (int i = start_at; i < num; i++) {
    AutocompleteMatch match(this, relevance_ - i, false,
                            AutocompleteMatch::URL_WHAT_YOU_TYPED);

    match.fill_into_edit = prefix_ + UTF8ToWide(base::IntToString(i));
    match.destination_url = GURL(WideToUTF8(match.fill_into_edit));

    match.contents = match.fill_into_edit;
    match.contents_class.push_back(
        ACMatchClassification(0, ACMatchClassification::NONE));
    match.description = match.fill_into_edit;
    match.description_class.push_back(
        ACMatchClassification(0, ACMatchClassification::NONE));

    matches_.push_back(match);
  }
}

class AutocompleteProviderTest : public testing::Test,
                                 public NotificationObserver {
 protected:
  // testing::Test
  virtual void SetUp();

  void ResetController(bool same_destinations);

  // Runs a query on the input "a", and makes sure both providers' input is
  // properly collected.
  void RunTest();

  // These providers are owned by the controller once it's created.
  ACProviders providers_;

  AutocompleteResult result_;

 private:
  // NotificationObserver
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

  MessageLoopForUI message_loop_;
  scoped_ptr<AutocompleteController> controller_;
  NotificationRegistrar registrar_;
};

void AutocompleteProviderTest::SetUp() {
  registrar_.Add(this, NotificationType::AUTOCOMPLETE_CONTROLLER_RESULT_UPDATED,
                 NotificationService::AllSources());
  ResetController(false);
}

void AutocompleteProviderTest::ResetController(bool same_destinations) {
  // Forget about any existing providers.  The controller owns them and will
  // Release() them below, when we delete it during the call to reset().
  providers_.clear();

  // Construct two new providers, with either the same or different prefixes.
  TestProvider* providerA = new TestProvider(num_results_per_provider,
                                             L"http://a");
  providerA->AddRef();
  providers_.push_back(providerA);

  TestProvider* providerB = new TestProvider(num_results_per_provider * 2,
      same_destinations ? L"http://a" : L"http://b");
  providerB->AddRef();
  providers_.push_back(providerB);

  // Reset the controller to contain our new providers.
  AutocompleteController* controller = new AutocompleteController(providers_);
  controller_.reset(controller);
  providerA->set_listener(controller);
  providerB->set_listener(controller);
}

void AutocompleteProviderTest::RunTest() {
  result_.Reset();
  controller_->Start(L"a", std::wstring(), true, false, false);

  // The message loop will terminate when all autocomplete input has been
  // collected.
  MessageLoop::current()->Run();
}

void AutocompleteProviderTest::Observe(NotificationType type,
                                       const NotificationSource& source,
                                       const NotificationDetails& details) {
  if (controller_->done()) {
    result_.CopyFrom(*(Details<const AutocompleteResult>(details).ptr()));
    MessageLoop::current()->Quit();
  }
}

// Tests that the default selection is set properly when updating results.
TEST_F(AutocompleteProviderTest, Query) {
  RunTest();

  // Make sure the default match gets set to the highest relevance match.  The
  // highest relevance matches should come from the second provider.
  EXPECT_EQ(num_results_per_provider * 2, result_.size());  // two providers
  ASSERT_NE(result_.end(), result_.default_match());
  EXPECT_EQ(providers_[1], result_.default_match()->provider);
}

TEST_F(AutocompleteProviderTest, RemoveDuplicates) {
  // Set up the providers to provide duplicate results.
  ResetController(true);

  RunTest();

  // Make sure all the first provider's results were eliminated by the second
  // provider's.
  EXPECT_EQ(num_results_per_provider, result_.size());
  for (AutocompleteResult::const_iterator i(result_.begin());
       i != result_.end(); ++i)
    EXPECT_EQ(providers_[1], i->provider);

  // Set things back to the default for the benefit of any tests that run after
  // us.
  ResetController(false);
}

TEST(AutocompleteTest, InputType) {
  struct test_data {
    const wchar_t* input;
    const AutocompleteInput::Type type;
  } input_cases[] = {
    { L"", AutocompleteInput::INVALID },
    { L"?", AutocompleteInput::FORCED_QUERY },
    { L"?foo", AutocompleteInput::FORCED_QUERY },
    { L"?foo bar", AutocompleteInput::FORCED_QUERY },
    { L"?http://foo.com/bar", AutocompleteInput::FORCED_QUERY },
    { L"foo", AutocompleteInput::UNKNOWN },
    { L"foo.c", AutocompleteInput::UNKNOWN },
    { L"foo.com", AutocompleteInput::URL },
    { L"-.com", AutocompleteInput::UNKNOWN },
    { L"foo/bar", AutocompleteInput::URL },
    { L"foo;bar", AutocompleteInput::QUERY },
    { L"foo/bar baz", AutocompleteInput::UNKNOWN },
    { L"foo bar.com", AutocompleteInput::QUERY },
    { L"foo bar", AutocompleteInput::QUERY },
    { L"foo+bar", AutocompleteInput::QUERY },
    { L"foo+bar.com", AutocompleteInput::UNKNOWN },
    { L"\"foo:bar\"", AutocompleteInput::QUERY },
    { L"link:foo.com", AutocompleteInput::UNKNOWN },
    { L"www.foo.com:81", AutocompleteInput::URL },
    { L"localhost:8080", AutocompleteInput::URL },
    { L"foo.com:123456", AutocompleteInput::QUERY },
    { L"foo.com:abc", AutocompleteInput::QUERY },
    { L"1.2.3.4:abc", AutocompleteInput::QUERY },
    { L"user@foo.com", AutocompleteInput::UNKNOWN },
    { L"user:pass@foo.com", AutocompleteInput::UNKNOWN },
    { L"1.2", AutocompleteInput::UNKNOWN },
    { L"1.2/45", AutocompleteInput::UNKNOWN },
    { L"1.2:45", AutocompleteInput::UNKNOWN },
    { L"user@1.2:45", AutocompleteInput::UNKNOWN },
    { L"user:foo@1.2:45", AutocompleteInput::UNKNOWN },
    { L"ps/2 games", AutocompleteInput::UNKNOWN },
    { L"en.wikipedia.org/wiki/James Bond", AutocompleteInput::URL },
    // In Chrome itself, mailto: will get handled by ShellExecute, but in
    // unittest mode, we don't have the data loaded in the external protocol
    // handler to know this.
    // { L"mailto:abuse@foo.com", AutocompleteInput::URL },
    { L"view-source:http://www.foo.com/", AutocompleteInput::URL },
    { L"javascript:alert(\"Hey there!\");", AutocompleteInput::URL },
#if defined(OS_WIN)
    { L"C:\\Program Files", AutocompleteInput::URL },
    { L"\\\\Server\\Folder\\File", AutocompleteInput::URL },
#endif  // defined(OS_WIN)
    { L"http:foo", AutocompleteInput::URL },
    { L"http://foo", AutocompleteInput::URL },
    { L"http://foo.c", AutocompleteInput::URL },
    { L"http://foo.com", AutocompleteInput::URL },
    { L"http://foo_bar.com", AutocompleteInput::URL },
    { L"http://foo/bar baz", AutocompleteInput::URL },
    { L"http://-.com", AutocompleteInput::UNKNOWN },
    { L"http://_foo_.com", AutocompleteInput::UNKNOWN },
    { L"http://foo.com:abc", AutocompleteInput::QUERY },
    { L"http://foo.com:123456", AutocompleteInput::QUERY },
    { L"http://1.2.3.4:abc", AutocompleteInput::QUERY },
    { L"http:user@foo.com", AutocompleteInput::URL },
    { L"http://user@foo.com", AutocompleteInput::URL },
    { L"http:user:pass@foo.com", AutocompleteInput::URL },
    { L"http://user:pass@foo.com", AutocompleteInput::URL },
    { L"http://1.2", AutocompleteInput::URL },
    { L"http://1.2/45", AutocompleteInput::URL },
    { L"http:ps/2 games", AutocompleteInput::URL },
    { L"http://ps/2 games", AutocompleteInput::URL },
    { L"https://foo.com", AutocompleteInput::URL },
    { L"127.0.0.1", AutocompleteInput::URL },
    { L"127.0.1", AutocompleteInput::UNKNOWN },
    { L"127.0.1/", AutocompleteInput::UNKNOWN },
    { L"browser.tabs.closeButtons", AutocompleteInput::UNKNOWN },
    { L"\u6d4b\u8bd5", AutocompleteInput::UNKNOWN },
    { L"[2001:]", AutocompleteInput::QUERY },  // Not a valid IP
    { L"[2001:dB8::1]", AutocompleteInput::URL },
    { L"192.168.0.256", AutocompleteInput::QUERY },  // Invalid IPv4 literal.
    { L"[foo.com]", AutocompleteInput::QUERY },  // Invalid IPv6 literal.
  };

  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(input_cases); ++i) {
    AutocompleteInput input(input_cases[i].input, std::wstring(), true, false,
                            false);
    EXPECT_EQ(input_cases[i].type, input.type()) << "Input: " <<
        input_cases[i].input;
  }
}

TEST(AutocompleteTest, InputTypeWithDesiredTLD) {
  struct test_data {
    const wchar_t* input;
    const AutocompleteInput::Type type;
  } input_cases[] = {
    { L"401k", AutocompleteInput::REQUESTED_URL },
    { L"999999999999999", AutocompleteInput::REQUESTED_URL },
  };

  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(input_cases); ++i) {
    AutocompleteInput input(input_cases[i].input, L"com", true, false, false);
    EXPECT_EQ(input_cases[i].type, input.type()) << "Input: " <<
        input_cases[i].input;
  }
}

// This tests for a regression where certain input in the omnibox caused us to
// crash. As long as the test completes without crashing, we're fine.
TEST(AutocompleteTest, InputCrash) {
  AutocompleteInput input(L"\uff65@s", std::wstring(), true, false, false);
}

// Test that we can properly compare matches' relevance when at least one is
// negative.
TEST(AutocompleteMatch, MoreRelevant) {
  struct RelevantCases {
    int r1;
    int r2;
    bool expected_result;
  } cases[] = {
    {  10,   0, true  },
    {  10,  -5, true  },
    {  -5,  10, false },
    {   0,  10, false },
    { -10,  -5, true  },
    {  -5, -10, false },
  };

  AutocompleteMatch m1(NULL, 0, false, AutocompleteMatch::URL_WHAT_YOU_TYPED);
  AutocompleteMatch m2(NULL, 0, false, AutocompleteMatch::URL_WHAT_YOU_TYPED);

  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(cases); ++i) {
    m1.relevance = cases[i].r1;
    m2.relevance = cases[i].r2;
    EXPECT_EQ(cases[i].expected_result,
              AutocompleteMatch::MoreRelevant(m1, m2));
  }
}

TEST(AutocompleteInput, ParseForEmphasizeComponent) {
  using url_parse::Component;
  Component kInvalidComponent(0, -1);
  struct test_data {
    const wchar_t* input;
    const Component scheme;
    const Component host;
  } input_cases[] = {
    { L"", kInvalidComponent, kInvalidComponent },
    { L"?", kInvalidComponent, kInvalidComponent },
    { L"?http://foo.com/bar", kInvalidComponent, kInvalidComponent },
    { L"foo/bar baz", kInvalidComponent, Component(0, 3) },
    { L"http://foo/bar baz", Component(0, 4), Component(7, 3) },
    { L"link:foo.com", Component(0, 4), kInvalidComponent },
    { L"www.foo.com:81", kInvalidComponent, Component(0, 11) },
    { L"\u6d4b\u8bd5", kInvalidComponent, Component(0, 2) },
    { L"view-source:http://www.foo.com/", Component(12, 4), Component(19, 11) },
    { L"view-source:https://example.com/",
      Component(12, 5), Component(20, 11) },
    { L"view-source:www.foo.com", kInvalidComponent, Component(12, 11) },
    { L"view-source:", Component(0, 11), kInvalidComponent },
    { L"view-source:garbage", kInvalidComponent, Component(12, 7) },
    { L"view-source:http://http://foo", Component(12, 4), Component(19, 4) },
    { L"view-source:view-source:http://example.com/",
      Component(12, 11), kInvalidComponent }
  };

  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(input_cases); ++i) {
    Component scheme, host;
    AutocompleteInput::ParseForEmphasizeComponents(input_cases[i].input,
                                                   std::wstring(),
                                                   &scheme,
                                                   &host);
    AutocompleteInput input(input_cases[i].input, std::wstring(), true, false,
                            false);
    EXPECT_EQ(input_cases[i].scheme.begin, scheme.begin) << "Input: " <<
        input_cases[i].input;
    EXPECT_EQ(input_cases[i].scheme.len, scheme.len) << "Input: " <<
        input_cases[i].input;
    EXPECT_EQ(input_cases[i].host.begin, host.begin) << "Input: " <<
        input_cases[i].input;
    EXPECT_EQ(input_cases[i].host.len, host.len) << "Input: " <<
        input_cases[i].input;
  }
}

}  // namespace
