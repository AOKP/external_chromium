// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_CLIENT_LOGIN_RESPONSE_HANDLER_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_CLIENT_LOGIN_RESPONSE_HANDLER_H_

#include <string>

#include "base/logging.h"
#include "chrome/browser/chromeos/login/auth_response_handler.h"

class URLRequestContextGetter;

namespace chromeos {

// Handles responses to a fetch executed upon the Google Accounts ClientLogin
// endpoint.  The cookies that are sent back in the response body are
// reformatted into a request for an time-limited authorization token, which
// is then sent to the IssueAuthToken endpoint.
class ClientLoginResponseHandler : public AuthResponseHandler {
 public:
  explicit ClientLoginResponseHandler(URLRequestContextGetter* getter)
      : getter_(getter) {}
  ~ClientLoginResponseHandler() {}

  // Overridden from AuthResponseHandler.
  virtual bool CanHandle(const GURL& url);

  // Overridden from AuthResponseHandler.
  // Takes in a response from ClientLogin, formats into an appropriate query
  // to sent to IssueAuthToken and issues said query.  |catcher| will receive
  // the response to the fetch.
  virtual URLFetcher* Handle(const std::string& to_process,
                             URLFetcher::Delegate* catcher);

  // exposed for tests.
  std::string payload() { return payload_; }

  // A string that tells the Accounts backend to which service we're trying to
  // authenticate.
  static const char kService[];
 private:
  std::string payload_;
  URLRequestContextGetter* getter_;

  DISALLOW_COPY_AND_ASSIGN(ClientLoginResponseHandler);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_CLIENT_LOGIN_RESPONSE_HANDLER_H_
