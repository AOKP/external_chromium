// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/net/url_request_slow_http_job.h"

#include "base/platform_thread.h"
#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "net/url_request/url_request_filter.h"

static const char kMockHostname[] = "mock.slow.http";

FilePath URLRequestSlowHTTPJob::base_path_;

// static
const int URLRequestSlowHTTPJob::kDelayMs = 1000;

using base::TimeDelta;

/* static */
URLRequestJob* URLRequestSlowHTTPJob::Factory(URLRequest* request,
                                              const std::string& scheme) {
  return new URLRequestSlowHTTPJob(request,
                                   GetOnDiskPath(base_path_, request, scheme));
}

/* static */
void URLRequestSlowHTTPJob::AddUrlHandler(const FilePath& base_path) {
  base_path_ = base_path;

  // Add kMockHostname to URLRequestFilter.
  URLRequestFilter* filter = URLRequestFilter::GetInstance();
  filter->AddHostnameHandler("http", kMockHostname,
                             URLRequestSlowHTTPJob::Factory);
}

/* static */
GURL URLRequestSlowHTTPJob::GetMockUrl(const FilePath& path) {
  std::string url = "http://";
  url.append(kMockHostname);
  url.append("/");
  url.append(WideToUTF8(path.ToWStringHack()));
  return GURL(url);
}

URLRequestSlowHTTPJob::URLRequestSlowHTTPJob(URLRequest* request,
                                             const FilePath& file_path)
    : URLRequestMockHTTPJob(request, file_path) { }

void URLRequestSlowHTTPJob::Start() {
  delay_timer_.Start(TimeDelta::FromMilliseconds(kDelayMs), this,
                     &URLRequestSlowHTTPJob::RealStart);
}

void URLRequestSlowHTTPJob::RealStart() {
  URLRequestMockHTTPJob::Start();
}
