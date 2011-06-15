// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/prerender/prerender_resource_handler.h"

#include "chrome/browser/net/chrome_url_request_context.h"
#include "chrome/common/resource_response.h"
#include "net/base/load_flags.h"
#include "net/http/http_response_headers.h"
#include "net/url_request/url_request.h"

namespace {

bool ShouldPrerender(const GURL& url, const ResourceResponse* response) {
  if (!response)
    return false;
  const ResourceResponseHead& rrh = response->response_head;
  if (!url.is_valid())
    return false;
  if (!rrh.headers)
    return false;
  if (!(url.SchemeIs("http") || url.SchemeIs("https")))
    return false;
  if (rrh.mime_type != "text/html")
    return false;
  if (rrh.headers->response_code() != 200)
    return false;
  return true;
}

}  // namespace

PrerenderResourceHandler* PrerenderResourceHandler::MaybeCreate(
    const net::URLRequest& request,
    ChromeURLRequestContext* context,
    ResourceHandler* next_handler) {
  if (!context || !context->prerender_manager())
    return NULL;
  if (!(request.load_flags() & net::LOAD_PREFETCH))
    return NULL;
  if (request.method() != "GET")
    return NULL;
  return new PrerenderResourceHandler(next_handler,
                                      context->prerender_manager());
}

PrerenderResourceHandler::PrerenderResourceHandler(
    ResourceHandler* next_handler,
    PrerenderManager* prerender_manager)
    : next_handler_(next_handler),
      prerender_manager_(prerender_manager),
      ALLOW_THIS_IN_INITIALIZER_LIST(
          prerender_callback_(NewCallback(
              this, &PrerenderResourceHandler::StartPrerender))) {
  DCHECK(next_handler);
  DCHECK(prerender_manager);
}

// This constructor is only used from unit tests.
PrerenderResourceHandler::PrerenderResourceHandler(
    ResourceHandler* next_handler,
    PrerenderCallback* callback)
    : next_handler_(next_handler),
      prerender_callback_(callback) {
  DCHECK(next_handler);
  DCHECK(callback);
}

PrerenderResourceHandler::~PrerenderResourceHandler() {
}

bool PrerenderResourceHandler::OnUploadProgress(int request_id,
                                                uint64 position,
                                                uint64 size) {
  return next_handler_->OnUploadProgress(request_id, position, size);
}

bool PrerenderResourceHandler::OnRequestRedirected(int request_id,
                                                   const GURL& url,
                                                   ResourceResponse* response,
                                                   bool* defer) {
  bool will_redirect = next_handler_->OnRequestRedirected(
      request_id, url, response, defer);
  if (will_redirect) {
    alias_urls_.push_back(url);
    url_ = url;
  }
  return will_redirect;
}

bool PrerenderResourceHandler::OnResponseStarted(int request_id,
                                                 ResourceResponse* response) {
  if (ShouldPrerender(url_, response)) {
    BrowserThread::PostTask(
        BrowserThread::UI,
        FROM_HERE,
        NewRunnableMethod(
            this,
            &PrerenderResourceHandler::RunCallbackFromUIThread,
            url_,
            alias_urls_));
  }
  return next_handler_->OnResponseStarted(request_id, response);
}

bool PrerenderResourceHandler::OnWillStart(int request_id,
                                           const GURL& url,
                                           bool* defer) {
  bool will_start = next_handler_->OnWillStart(request_id, url, defer);
  if (will_start) {
    alias_urls_.push_back(url);
    url_ = url;
  }
  return will_start;
}

bool PrerenderResourceHandler::OnWillRead(int request_id,
                                          net::IOBuffer** buf,
                                          int* buf_size,
                                          int min_size) {
  return next_handler_->OnWillRead(request_id, buf, buf_size, min_size);
}

bool PrerenderResourceHandler::OnReadCompleted(int request_id,
                                               int* bytes_read) {
  return next_handler_->OnReadCompleted(request_id, bytes_read);
}

bool PrerenderResourceHandler::OnResponseCompleted(
    int request_id,
    const net::URLRequestStatus& status,
    const std::string& security_info) {
  return next_handler_->OnResponseCompleted(request_id, status, security_info);
}

void PrerenderResourceHandler::OnRequestClosed() {
  next_handler_->OnRequestClosed();
}

void PrerenderResourceHandler::RunCallbackFromUIThread(const GURL& url,
                                                       const std::vector<GURL>&
                                                       alias_urls) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  prerender_callback_->Run(url, alias_urls);
}

void PrerenderResourceHandler::StartPrerender(const GURL& url,
                                              const std::vector<GURL>&
                                              alias_urls) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  prerender_manager_->AddPreload(url, alias_urls);
}
