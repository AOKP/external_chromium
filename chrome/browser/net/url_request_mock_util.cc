// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/net/url_request_mock_util.h"

#include <string>

#include "base/path_service.h"
#include "chrome/browser/chrome_thread.h"
#include "chrome/browser/net/url_request_failed_dns_job.h"
#include "chrome/browser/net/url_request_mock_http_job.h"
#include "chrome/browser/net/url_request_mock_link_doctor_job.h"
#include "chrome/browser/net/url_request_slow_download_job.h"
#include "chrome/browser/net/url_request_slow_http_job.h"
#include "chrome/common/chrome_paths.h"
#include "net/url_request/url_request_filter.h"

namespace chrome_browser_net {

void SetUrlRequestMocksEnabled(bool enabled) {
  // Since this involves changing the URLRequest ProtocolFactory, we need to
  // run on the IO thread.
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::IO));

  if (enabled) {
    URLRequestFilter::GetInstance()->ClearHandlers();

    URLRequestFailedDnsJob::AddUrlHandler();
    URLRequestMockLinkDoctorJob::AddUrlHandler();
    URLRequestSlowDownloadJob::AddUrlHandler();

    FilePath root_http;
    PathService::Get(chrome::DIR_TEST_DATA, &root_http);
    URLRequestMockHTTPJob::AddUrlHandler(root_http);
    URLRequestSlowHTTPJob::AddUrlHandler(root_http);
  } else {
    // Revert to the default handlers.
    URLRequestFilter::GetInstance()->ClearHandlers();
  }
}

}  // namespace chrome_browser_net
