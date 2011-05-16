// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/renderer_host/offline_resource_handler.h"

#include <vector>

#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/singleton.h"
#include "base/string_util.h"
#include "chrome/browser/browser_thread.h"
#include "chrome/browser/chromeos/network_state_notifier.h"
#include "chrome/browser/chromeos/offline/offline_load_page.h"
#include "chrome/browser/chromeos/offline/offline_load_service.h"
#include "chrome/browser/renderer_host/resource_dispatcher_host.h"
#include "chrome/browser/renderer_host/resource_dispatcher_host_request_info.h"
#include "chrome/common/url_constants.h"

OfflineResourceHandler::OfflineResourceHandler(
    ResourceHandler* handler,
    int host_id,
    int route_id,
    ResourceDispatcherHost* rdh,
    net::URLRequest* request)
    : next_handler_(handler),
      process_host_id_(host_id),
      render_view_id_(route_id),
      rdh_(rdh),
      request_(request),
      deferred_request_id_(-1) {
}

bool OfflineResourceHandler::OnUploadProgress(int request_id,
                                               uint64 position,
                                               uint64 size) {
  return next_handler_->OnUploadProgress(request_id, position, size);
}

bool OfflineResourceHandler::OnRequestRedirected(int request_id,
                                                 const GURL& new_url,
                                                 ResourceResponse* response,
                                                 bool* defer) {
  return next_handler_->OnRequestRedirected(
      request_id, new_url, response, defer);
}

bool OfflineResourceHandler::OnResponseStarted(int request_id,
                                                ResourceResponse* response) {
  return next_handler_->OnResponseStarted(request_id, response);
}

bool OfflineResourceHandler::OnResponseCompleted(
    int request_id,
    const URLRequestStatus& status,
    const std::string& security_info) {
  return next_handler_->OnResponseCompleted(request_id, status, security_info);
}

void OfflineResourceHandler::OnRequestClosed() {
  next_handler_->OnRequestClosed();
}

bool OfflineResourceHandler::OnWillStart(int request_id,
                                         const GURL& url,
                                         bool* defer) {
  if (ShouldShowOfflinePage(url)) {
    deferred_request_id_ = request_id;
    deferred_url_ = url;
    DVLOG(1) << "WillStart: this=" << this << ", request id=" << request_id;
    AddRef();  //  Balanced with OnBlockingPageComplete
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        NewRunnableMethod(this, &OfflineResourceHandler::ShowOfflinePage));
    *defer = true;
    return true;
  }
  return next_handler_->OnWillStart(request_id, url, defer);
}

// We'll let the original event handler provide a buffer, and reuse it for
// subsequent reads until we're done buffering.
bool OfflineResourceHandler::OnWillRead(int request_id, net::IOBuffer** buf,
                                         int* buf_size, int min_size) {
  return next_handler_->OnWillRead(request_id, buf, buf_size, min_size);
}

bool OfflineResourceHandler::OnReadCompleted(int request_id, int* bytes_read) {
  return next_handler_->OnReadCompleted(request_id, bytes_read);
}

void OfflineResourceHandler::OnBlockingPageComplete(bool proceed) {
  if (deferred_request_id_ < 0) {
    LOG(WARNING) << "OnBlockingPageComplete called after completion: "
                 << " this=" << this << ", request_id="
                 << deferred_request_id_;
    NOTREACHED();
    return;
  }
  if (!BrowserThread::CurrentlyOn(BrowserThread::IO)) {
    BrowserThread::PostTask(
        BrowserThread::IO, FROM_HERE,
        NewRunnableMethod(this,
                          &OfflineResourceHandler::OnBlockingPageComplete,
                          proceed));
    return;
  }
  if (proceed) {
    Resume();
  } else {
    int request_id = deferred_request_id_;
    ClearRequestInfo();
    rdh_->CancelRequest(process_host_id_, request_id, false);
  }
  Release();  // Balanced with OnWillStart
}

void OfflineResourceHandler::ClearRequestInfo() {
  deferred_url_ = GURL();
  deferred_request_id_ = -1;
}

bool OfflineResourceHandler::IsRemote(const GURL& url) const {
  return url.SchemeIs(chrome::kFtpScheme) ||
         url.SchemeIs(chrome::kHttpScheme) ||
         url.SchemeIs(chrome::kHttpsScheme);
}

bool OfflineResourceHandler::ShouldShowOfflinePage(const GURL& url) const {
  // Only check main frame. If the network is disconnected while
  // loading other resources, we'll simply show broken link/images.
  return IsRemote(url) &&
      !chromeos::NetworkStateNotifier::is_connected() &&
      ResourceDispatcherHost::InfoForRequest(request_)->resource_type()
          == ResourceType::MAIN_FRAME &&
      !chromeos::OfflineLoadService::Get()->ShouldProceed(
          process_host_id_, render_view_id_, url);
}

void OfflineResourceHandler::Resume() {
  const GURL url = deferred_url_;
  int request_id = deferred_request_id_;
  ClearRequestInfo();

  chromeos::OfflineLoadService::Get()->Proceeded(
      process_host_id_, render_view_id_, url);

  DCHECK_NE(request_id, -1);
  bool defer = false;
  DVLOG(1) << "Resume load: this=" << this << ", request id=" << request_id;
  next_handler_->OnWillStart(request_id, url, &defer);
  if (!defer)
    rdh_->StartDeferredRequest(process_host_id_, request_id);
}

void OfflineResourceHandler::ShowOfflinePage() {
  chromeos::OfflineLoadPage::Show(
      process_host_id_, render_view_id_, deferred_url_, this);
}
