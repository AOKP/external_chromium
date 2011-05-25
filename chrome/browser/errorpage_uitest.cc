// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/string_util.h"
#include "base/threading/platform_thread.h"
#include "chrome/test/automation/tab_proxy.h"
#include "chrome/test/ui/ui_test.h"
#include "chrome/browser/net/url_request_failed_dns_job.h"
#include "chrome/browser/net/url_request_mock_http_job.h"
#include "net/test/test_server.h"

class ErrorPageTest : public UITest {
 protected:
  bool WaitForTitleMatching(const std::wstring& title) {
    for (int i = 0; i < 10; ++i) {
      if (GetActiveTabTitle() == title)
        return true;
      base::PlatformThread::Sleep(sleep_timeout_ms());
    }
    EXPECT_EQ(title, GetActiveTabTitle());
    return false;
  }
};

TEST_F(ErrorPageTest, DNSError_Basic) {
  GURL test_url(URLRequestFailedDnsJob::kTestUrl);

  // The first navigation should fail, and the second one should be the error
  // page.
  NavigateToURLBlockUntilNavigationsComplete(test_url, 2);

  EXPECT_TRUE(WaitForTitleMatching(L"Mock Link Doctor"));
}

TEST_F(ErrorPageTest, DNSError_GoBack1) {
  // Test that a DNS error occuring in the main frame does not result in an
  // additional session history entry.
  GURL test_url(URLRequestFailedDnsJob::kTestUrl);

  NavigateToURL(URLRequestMockHTTPJob::GetMockUrl(
                    FilePath(FILE_PATH_LITERAL("title2.html"))));
  // The first navigation should fail, and the second one should be the error
  // page.
  NavigateToURLBlockUntilNavigationsComplete(test_url, 2);
  EXPECT_TRUE(WaitForTitleMatching(L"Mock Link Doctor"));

  EXPECT_TRUE(GetActiveTab()->GoBack());

  EXPECT_TRUE(WaitForTitleMatching(L"Title Of Awesomeness"));
}

TEST_F(ErrorPageTest, DNSError_GoBack2) {
  // Test that a DNS error occuring in the main frame does not result in an
  // additional session history entry.
  GURL test_url(URLRequestFailedDnsJob::kTestUrl);

  NavigateToURL(URLRequestMockHTTPJob::GetMockUrl(
                    FilePath(FILE_PATH_LITERAL("title2.html"))));
  // The first navigation should fail, and the second one should be the error
  // page.
  NavigateToURLBlockUntilNavigationsComplete(test_url, 2);
  EXPECT_TRUE(WaitForTitleMatching(L"Mock Link Doctor"));
  NavigateToURL(URLRequestMockHTTPJob::GetMockUrl(
                    FilePath(FILE_PATH_LITERAL("title3.html"))));

  // The first navigation should fail, and the second one should be the error
  // page.
  EXPECT_TRUE(GetActiveTab()->GoBackBlockUntilNavigationsComplete(2));
  EXPECT_TRUE(WaitForTitleMatching(L"Mock Link Doctor"));
  EXPECT_TRUE(GetActiveTab()->GoBack());

  EXPECT_TRUE(WaitForTitleMatching(L"Title Of Awesomeness"));
}

TEST_F(ErrorPageTest, DNSError_GoBack2AndForward) {
  // Test that a DNS error occuring in the main frame does not result in an
  // additional session history entry.

  GURL test_url(URLRequestFailedDnsJob::kTestUrl);

  NavigateToURL(URLRequestMockHTTPJob::GetMockUrl(
                    FilePath(FILE_PATH_LITERAL("title2.html"))));
  // The first navigation should fail, and the second one should be the error
  // page.
  NavigateToURLBlockUntilNavigationsComplete(test_url, 2);
  EXPECT_TRUE(WaitForTitleMatching(L"Mock Link Doctor"));
  NavigateToURL(URLRequestMockHTTPJob::GetMockUrl(
                    FilePath(FILE_PATH_LITERAL("title3.html"))));

  // The first navigation should fail, and the second one should be the error
  // page.
  EXPECT_TRUE(GetActiveTab()->GoBackBlockUntilNavigationsComplete(2));
  EXPECT_TRUE(WaitForTitleMatching(L"Mock Link Doctor"));
  EXPECT_TRUE(GetActiveTab()->GoBack());
  // The first navigation should fail, and the second one should be the error
  // page.
  EXPECT_TRUE(GetActiveTab()->GoForwardBlockUntilNavigationsComplete(2));

  EXPECT_TRUE(WaitForTitleMatching(L"Mock Link Doctor"));
}

TEST_F(ErrorPageTest, DNSError_GoBack2Forward2) {
  // Test that a DNS error occuring in the main frame does not result in an
  // additional session history entry.

  GURL test_url(URLRequestFailedDnsJob::kTestUrl);

  NavigateToURL(URLRequestMockHTTPJob::GetMockUrl(
                    FilePath(FILE_PATH_LITERAL("title3.html"))));
  // The first navigation should fail, and the second one should be the error
  // page.
  NavigateToURLBlockUntilNavigationsComplete(test_url, 2);
  EXPECT_TRUE(WaitForTitleMatching(L"Mock Link Doctor"));
  NavigateToURL(URLRequestMockHTTPJob::GetMockUrl(
                    FilePath(FILE_PATH_LITERAL("title2.html"))));

  // The first navigation should fail, and the second one should be the error
  // page.
  EXPECT_TRUE(GetActiveTab()->GoBackBlockUntilNavigationsComplete(2));
  EXPECT_TRUE(WaitForTitleMatching(L"Mock Link Doctor"));
  EXPECT_TRUE(GetActiveTab()->GoBack());
  // The first navigation should fail, and the second one should be the error
  // page.
  EXPECT_TRUE(GetActiveTab()->GoForwardBlockUntilNavigationsComplete(2));
  EXPECT_TRUE(WaitForTitleMatching(L"Mock Link Doctor"));
  EXPECT_TRUE(GetActiveTab()->GoForward());

  EXPECT_TRUE(WaitForTitleMatching(L"Title Of Awesomeness"));
}

TEST_F(ErrorPageTest, IFrameDNSError_Basic) {
  NavigateToURL(URLRequestMockHTTPJob::GetMockUrl(
                    FilePath(FILE_PATH_LITERAL("iframe_dns_error.html"))));
  EXPECT_TRUE(WaitForTitleMatching(L"Blah"));
}

TEST_F(ErrorPageTest, IFrameDNSError_GoBack) {
  // Test that a DNS error occuring in an iframe does not result in an
  // additional session history entry.

  NavigateToURL(URLRequestMockHTTPJob::GetMockUrl(
                    FilePath(FILE_PATH_LITERAL("title2.html"))));
  NavigateToURL(URLRequestMockHTTPJob::GetMockUrl(
                    FilePath(FILE_PATH_LITERAL("iframe_dns_error.html"))));

  EXPECT_TRUE(GetActiveTab()->GoBack());

  EXPECT_TRUE(WaitForTitleMatching(L"Title Of Awesomeness"));
}

TEST_F(ErrorPageTest, IFrameDNSError_GoBackAndForward) {
  // Test that a DNS error occuring in an iframe does not result in an
  // additional session history entry.

  NavigateToURL(URLRequestMockHTTPJob::GetMockUrl(
                    FilePath(FILE_PATH_LITERAL("title2.html"))));
  NavigateToURL(URLRequestMockHTTPJob::GetMockUrl(
                    FilePath(FILE_PATH_LITERAL("iframe_dns_error.html"))));

  EXPECT_TRUE(GetActiveTab()->GoBack());
  EXPECT_TRUE(GetActiveTab()->GoForward());

  EXPECT_TRUE(WaitForTitleMatching(L"Blah"));
}

#if defined(OS_WIN)
// Might be related to http://crbug.com/60937
#define MAYBE_IFrame404 FLAKY_IFrame404
#else
#define MAYBE_IFrame404 IFrame404
#endif

TEST_F(ErrorPageTest, MAYBE_IFrame404) {
  // iframes that have 404 pages should not trigger an alternate error page.
  // In this test, the iframe sets the title of the parent page to "SUCCESS"
  // when the iframe loads.  If the iframe fails to load (because an alternate
  // error page loads instead), then the title will remain as "FAIL".
  net::TestServer test_server(net::TestServer::TYPE_HTTP,
                              FilePath(FILE_PATH_LITERAL("chrome/test/data")));
  ASSERT_TRUE(test_server.Start());
  NavigateToURL(test_server.GetURL("files/iframe404.html"));
  EXPECT_TRUE(WaitForTitleMatching(L"SUCCESS"));
}

TEST_F(ErrorPageTest, Page404) {
  NavigateToURL(URLRequestMockHTTPJob::GetMockUrl(
                    FilePath(FILE_PATH_LITERAL("title2.html"))));
  // The first navigation should fail, and the second one should be the error
  // page.
  NavigateToURLBlockUntilNavigationsComplete(
      URLRequestMockHTTPJob::GetMockUrl(
          FilePath(FILE_PATH_LITERAL("page404.html"))), 2);

  EXPECT_TRUE(WaitForTitleMatching(L"Mock Link Doctor"));
}

TEST_F(ErrorPageTest, Page404_GoBack) {
  NavigateToURL(URLRequestMockHTTPJob::GetMockUrl(
                    FilePath(FILE_PATH_LITERAL("title2.html"))));
  // The first navigation should fail, and the second one should be the error
  // page.
  NavigateToURLBlockUntilNavigationsComplete(
      URLRequestMockHTTPJob::GetMockUrl(
          FilePath(FILE_PATH_LITERAL("page404.html"))), 2);
  EXPECT_TRUE(WaitForTitleMatching(L"Mock Link Doctor"));

  EXPECT_TRUE(GetActiveTab()->GoBack());

  EXPECT_TRUE(WaitForTitleMatching(L"Title Of Awesomeness"));
}
