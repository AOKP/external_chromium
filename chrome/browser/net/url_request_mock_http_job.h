// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// A URLRequestJob class that pulls the content and http headers from disk.

#ifndef CHROME_BROWSER_NET_URL_REQUEST_MOCK_HTTP_JOB_H_
#define CHROME_BROWSER_NET_URL_REQUEST_MOCK_HTTP_JOB_H_

#include <string>

#include "net/url_request/url_request_file_job.h"

class FilePath;

class URLRequestMockHTTPJob : public URLRequestFileJob {
 public:
  URLRequestMockHTTPJob(URLRequest* request, const FilePath& file_path);

  virtual bool GetMimeType(std::string* mime_type) const;
  virtual bool GetCharset(std::string* charset);
  virtual void GetResponseInfo(net::HttpResponseInfo* info);
  virtual bool IsRedirectResponse(GURL* location, int* http_status_code);

  static URLRequest::ProtocolFactory Factory;

  // Adds the testing URLs to the URLRequestFilter.
  static void AddUrlHandler(const FilePath& base_path);

  // Given the path to a file relative to base_path_, construct a mock URL.
  static GURL GetMockUrl(const FilePath& path);

  // Given the path to a file relative to base_path_,
  // construct a mock URL for view source.
  static GURL GetMockViewSourceUrl(const FilePath& path);

 protected:
  virtual ~URLRequestMockHTTPJob() { }

  static FilePath GetOnDiskPath(const FilePath& base_path,
                                URLRequest* request,
                                const std::string& scheme);

 private:
  void GetResponseInfoConst(net::HttpResponseInfo* info) const;

  // This is the file path leading to the root of the directory to use as the
  // root of the http server.
  static FilePath base_path_;
};

#endif  // CHROME_BROWSER_NET_URL_REQUEST_MOCK_HTTP_JOB_H_
