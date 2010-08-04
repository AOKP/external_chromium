// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NET_CONNECT_INTERCEPTOR_H_
#define CHROME_BROWSER_NET_CONNECT_INTERCEPTOR_H_

#include "net/url_request/url_request.h"

namespace chrome_browser_net {

//------------------------------------------------------------------------------
// An interceptor to monitor URLRequests so that we can do speculative DNS
// resolution and/or speculative TCP preconnections.
class ConnectInterceptor : public URLRequest::Interceptor {
 public:
  // Construction includes registration as an URL.
  ConnectInterceptor();
  // Destruction includes unregistering.
  virtual ~ConnectInterceptor();

 protected:
  // URLRequest::Interceptor overrides
  // Learn about referrers, and optionally preconnect based on history.
  virtual URLRequestJob* MaybeIntercept(URLRequest* request);
  virtual URLRequestJob* MaybeInterceptResponse(URLRequest* request) {
    return NULL;
  }
  virtual URLRequestJob* MaybeInterceptRedirect(URLRequest* request,
                                                const GURL& location) {
    return NULL;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ConnectInterceptor);
};

}  // namespace chrome_browser_net

#endif  // CHROME_BROWSER_NET_CONNECT_INTERCEPTOR_H_
