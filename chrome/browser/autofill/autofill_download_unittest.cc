// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <list>

#include "base/string_util.h"
#include "base/test/test_timeouts.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/autofill/autofill_download.h"
#include "chrome/common/net/test_url_fetcher_factory.h"
#include "chrome/test/testing_profile.h"
#include "net/url_request/url_request_status.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/WebKit/WebKit/chromium/public/WebInputElement.h"
#include "webkit/glue/form_data.h"

using webkit_glue::FormData;
using WebKit::WebInputElement;

// This tests AutoFillDownloadManager. AutoFillDownloadTestHelper implements
// AutoFillDownloadManager::Observer and creates an instance of
// AutoFillDownloadManager. Then it records responses to different initiated
// requests, which are verified later. To mock network requests
// TestURLFetcherFactory is used, which creates URLFetchers that do not
// go over the wire, but allow calling back HTTP responses directly.
// The responses in test are out of order and verify: successful query request,
// successful upload request, failed upload request.
class AutoFillDownloadTestHelper : public AutoFillDownloadManager::Observer {
 public:
  AutoFillDownloadTestHelper()
      : download_manager(&profile) {
    download_manager.SetObserver(this);
    // For chromium builds forces Start*Request to actually execute.
    download_manager.is_testing_ = true;
  }
  ~AutoFillDownloadTestHelper() {
    download_manager.SetObserver(NULL);
  }

  // AutoFillDownloadManager::Observer overridables:
  virtual void OnLoadedAutoFillHeuristics(
      const std::string& heuristic_xml) {
    ResponseData response;
    response.response = heuristic_xml;
    response.type_of_response = QUERY_SUCCESSFULL;
    responses_.push_back(response);
  };

  virtual void OnUploadedAutoFillHeuristics(const std::string& form_signature) {
    ResponseData response;
    response.type_of_response = UPLOAD_SUCCESSFULL;
    responses_.push_back(response);
  }
  virtual void OnHeuristicsRequestError(
      const std::string& form_signature,
      AutoFillDownloadManager::AutoFillRequestType request_type,
      int http_error) {
    ResponseData response;
    response.signature = form_signature;
    response.error = http_error;
    response.type_of_response =
        request_type == AutoFillDownloadManager::REQUEST_QUERY ?
            REQUEST_QUERY_FAILED : REQUEST_UPLOAD_FAILED;
    responses_.push_back(response);
  }

  enum TYPE_OF_RESPONSE {
    QUERY_SUCCESSFULL,
    UPLOAD_SUCCESSFULL,
    REQUEST_QUERY_FAILED,
    REQUEST_UPLOAD_FAILED,
  };

  struct ResponseData {
    TYPE_OF_RESPONSE type_of_response;
    int error;
    std::string signature;
    std::string response;
    ResponseData() : type_of_response(REQUEST_QUERY_FAILED), error(0) {
    }
  };
  std::list<AutoFillDownloadTestHelper::ResponseData> responses_;

  TestingProfile profile;
  AutoFillDownloadManager download_manager;
};

namespace {

TEST(AutoFillDownloadTest, QueryAndUploadTest) {
  MessageLoopForUI message_loop;
  // Create and register factory.
  AutoFillDownloadTestHelper helper;
  TestURLFetcherFactory factory;
  URLFetcher::set_factory(&factory);

  FormData form;
  form.method = ASCIIToUTF16("post");
  form.fields.push_back(webkit_glue::FormField(ASCIIToUTF16("username"),
                                               ASCIIToUTF16("username"),
                                               string16(),
                                               ASCIIToUTF16("text"),
                                               0,
                                               false));
  form.fields.push_back(webkit_glue::FormField(ASCIIToUTF16("First Name"),
                                               ASCIIToUTF16("firstname"),
                                               string16(),
                                               ASCIIToUTF16("text"),
                                               0,
                                               false));
  form.fields.push_back(webkit_glue::FormField(ASCIIToUTF16("Last Name"),
                                               ASCIIToUTF16("lastname"),
                                               string16(),
                                               ASCIIToUTF16("text"),
                                               0,
                                               false));
  form.fields.push_back(webkit_glue::FormField(ASCIIToUTF16("email"),
                                               ASCIIToUTF16("email"),
                                               string16(),
                                               ASCIIToUTF16("text"),
                                               0,
                                               false));
  form.fields.push_back(webkit_glue::FormField(ASCIIToUTF16("email2"),
                                               ASCIIToUTF16("email2"),
                                               string16(),
                                               ASCIIToUTF16("text"),
                                               0,
                                               false));
  form.fields.push_back(webkit_glue::FormField(ASCIIToUTF16("password"),
                                               ASCIIToUTF16("password"),
                                               string16(),
                                               ASCIIToUTF16("password"),
                                               0,
                                               false));
  form.fields.push_back(webkit_glue::FormField(string16(),
                                               ASCIIToUTF16("Submit"),
                                               string16(),
                                               ASCIIToUTF16("submit"),
                                               0,
                                               false));

  FormStructure *form_structure = new FormStructure(form);
  ScopedVector<FormStructure> form_structures;
  form_structures.push_back(form_structure);

  form.fields.clear();
  form.fields.push_back(webkit_glue::FormField(ASCIIToUTF16("address"),
                                               ASCIIToUTF16("address"),
                                               string16(),
                                               ASCIIToUTF16("text"),
                                               0,
                                               false));
  form.fields.push_back(webkit_glue::FormField(ASCIIToUTF16("address2"),
                                               ASCIIToUTF16("address2"),
                                               string16(),
                                               ASCIIToUTF16("text"),
                                               0,
                                               false));
  form.fields.push_back(webkit_glue::FormField(ASCIIToUTF16("city"),
                                               ASCIIToUTF16("city"),
                                               string16(),
                                               ASCIIToUTF16("text"),
                                               0,
                                               false));
  form.fields.push_back(webkit_glue::FormField(string16(),
                                               ASCIIToUTF16("Submit"),
                                               string16(),
                                               ASCIIToUTF16("submit"),
                                               0,
                                               false));
  form_structure = new FormStructure(form);
  form_structures.push_back(form_structure);

  // Request with id 0.
  EXPECT_TRUE(helper.download_manager.StartQueryRequest(form_structures));
  // Set upload to 100% so requests happen.
  helper.download_manager.SetPositiveUploadRate(1.0);
  helper.download_manager.SetNegativeUploadRate(1.0);
  // Request with id 1.
  EXPECT_TRUE(helper.download_manager.StartUploadRequest(*(form_structures[0]),
                                                         true));
  // Request with id 2.
  EXPECT_TRUE(helper.download_manager.StartUploadRequest(*(form_structures[1]),
                                                         false));

  const char *responses[] = {
    "<autofillqueryresponse>"
      "<field autofilltype=\"0\" />"
      "<field autofilltype=\"3\" />"
      "<field autofilltype=\"5\" />"
      "<field autofilltype=\"9\" />"
      "<field autofilltype=\"0\" />"
      "<field autofilltype=\"30\" />"
      "<field autofilltype=\"31\" />"
      "<field autofilltype=\"33\" />"
    "</autofillqueryresponse>",
    "<autofilluploadresponse positiveuploadrate=\"0.5\" "
    "negativeuploadrate=\"0.3\"/>",
    "<html></html>",
  };

  // Return them out of sequence.
  TestURLFetcher* fetcher = factory.GetFetcherByID(1);
  ASSERT_TRUE(fetcher);
  fetcher->delegate()->OnURLFetchComplete(fetcher, GURL(), URLRequestStatus(),
                                          200, ResponseCookies(),
                                          std::string(responses[1]));
  // After that upload rates would be adjusted to 0.5/0.3
  EXPECT_DOUBLE_EQ(0.5, helper.download_manager.GetPositiveUploadRate());
  EXPECT_DOUBLE_EQ(0.3, helper.download_manager.GetNegativeUploadRate());

  fetcher = factory.GetFetcherByID(2);
  ASSERT_TRUE(fetcher);
  fetcher->delegate()->OnURLFetchComplete(fetcher, GURL(), URLRequestStatus(),
                                          404, ResponseCookies(),
                                          std::string(responses[2]));
  fetcher = factory.GetFetcherByID(0);
  ASSERT_TRUE(fetcher);
  fetcher->delegate()->OnURLFetchComplete(fetcher, GURL(), URLRequestStatus(),
                                          200, ResponseCookies(),
                                          std::string(responses[0]));
  EXPECT_EQ(static_cast<size_t>(3), helper.responses_.size());

  EXPECT_EQ(AutoFillDownloadTestHelper::UPLOAD_SUCCESSFULL,
            helper.responses_.front().type_of_response);
  EXPECT_EQ(0, helper.responses_.front().error);
  EXPECT_EQ(std::string(), helper.responses_.front().signature);
  // Expected response on non-query request is an empty string.
  EXPECT_EQ(std::string(), helper.responses_.front().response);
  helper.responses_.pop_front();

  EXPECT_EQ(AutoFillDownloadTestHelper::REQUEST_UPLOAD_FAILED,
            helper.responses_.front().type_of_response);
  EXPECT_EQ(404, helper.responses_.front().error);
  EXPECT_EQ(form_structures[1]->FormSignature(),
            helper.responses_.front().signature);
  // Expected response on non-query request is an empty string.
  EXPECT_EQ(std::string(), helper.responses_.front().response);
  helper.responses_.pop_front();

  EXPECT_EQ(helper.responses_.front().type_of_response,
            AutoFillDownloadTestHelper::QUERY_SUCCESSFULL);
  EXPECT_EQ(0, helper.responses_.front().error);
  EXPECT_EQ(std::string(), helper.responses_.front().signature);
  EXPECT_EQ(responses[0], helper.responses_.front().response);
  helper.responses_.pop_front();

  // Set upload to 0% so no new requests happen.
  helper.download_manager.SetPositiveUploadRate(0.0);
  helper.download_manager.SetNegativeUploadRate(0.0);
  // No actual requests for the next two calls, as we set upload rate to 0%.
  EXPECT_FALSE(helper.download_manager.StartUploadRequest(*(form_structures[0]),
                                                         true));
  EXPECT_FALSE(helper.download_manager.StartUploadRequest(*(form_structures[1]),
                                                         false));
  fetcher = factory.GetFetcherByID(3);
  EXPECT_EQ(NULL, fetcher);

  // Request with id 3.
  EXPECT_TRUE(helper.download_manager.StartQueryRequest(form_structures));
  fetcher = factory.GetFetcherByID(3);
  ASSERT_TRUE(fetcher);
  fetcher->set_backoff_delay(
      base::TimeDelta::FromMilliseconds(TestTimeouts::action_max_timeout_ms()));
  fetcher->delegate()->OnURLFetchComplete(fetcher, GURL(), URLRequestStatus(),
                                          500, ResponseCookies(),
                                          std::string(responses[0]));
  EXPECT_EQ(AutoFillDownloadTestHelper::REQUEST_QUERY_FAILED,
            helper.responses_.front().type_of_response);
  EXPECT_EQ(500, helper.responses_.front().error);
  // Expected response on non-query request is an empty string.
  EXPECT_EQ(std::string(), helper.responses_.front().response);
  helper.responses_.pop_front();

  // Query requests should be ignored for the next 10 seconds.
  EXPECT_FALSE(helper.download_manager.StartQueryRequest(form_structures));
  fetcher = factory.GetFetcherByID(4);
  EXPECT_EQ(NULL, fetcher);

  // Set upload to 100% so requests happen.
  helper.download_manager.SetPositiveUploadRate(1.0);
  // Request with id 4.
  EXPECT_TRUE(helper.download_manager.StartUploadRequest(*(form_structures[0]),
              true));
  fetcher = factory.GetFetcherByID(4);
  ASSERT_TRUE(fetcher);
  fetcher->set_backoff_delay(
      base::TimeDelta::FromMilliseconds(TestTimeouts::action_max_timeout_ms()));
  fetcher->delegate()->OnURLFetchComplete(fetcher, GURL(), URLRequestStatus(),
                                          503, ResponseCookies(),
                                          std::string(responses[2]));
  EXPECT_EQ(AutoFillDownloadTestHelper::REQUEST_UPLOAD_FAILED,
            helper.responses_.front().type_of_response);
  EXPECT_EQ(503, helper.responses_.front().error);
  helper.responses_.pop_front();

  // Upload requests should be ignored for the next 10 seconds.
  EXPECT_FALSE(helper.download_manager.StartUploadRequest(*(form_structures[0]),
              true));
  fetcher = factory.GetFetcherByID(5);
  EXPECT_EQ(NULL, fetcher);

  // Make sure consumer of URLFetcher does the right thing.
  URLFetcher::set_factory(NULL);
}

}  // namespace
