// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/dom_ui/dom_ui_thumbnail_source.h"

#include "app/resource_bundle.h"
#include "base/callback.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/history/top_sites.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/url_constants.h"
#include "googleurl/src/gurl.h"
#include "grit/theme_resources.h"

DOMUIThumbnailSource::DOMUIThumbnailSource(Profile* profile)
    : DataSource(chrome::kChromeUIThumbnailPath, MessageLoop::current()),
      profile_(profile) {
  if (history::TopSites::IsEnabled()) {
    // Set TopSites now as Profile isn't thread safe.
    top_sites_ = profile_->GetTopSites();
  }
}

DOMUIThumbnailSource::~DOMUIThumbnailSource() {
}

void DOMUIThumbnailSource::StartDataRequest(const std::string& path,
                                            bool is_off_the_record,
                                            int request_id) {
  if (top_sites_.get()) {
    scoped_refptr<RefCountedBytes> data;
    if (top_sites_->GetPageThumbnail(GURL(path), &data)) {
      // We have the thumbnail.
      SendResponse(request_id, data.get());
    } else {
      SendDefaultThumbnail(request_id);
    }
    return;
  }

  HistoryService* hs = profile_->GetHistoryService(Profile::EXPLICIT_ACCESS);
  if (hs) {
    HistoryService::Handle handle = hs->GetPageThumbnail(
        GURL(path),
        &cancelable_consumer_,
        NewCallback(this, &DOMUIThumbnailSource::OnThumbnailDataAvailable));
    // Attach the ChromeURLDataManager request ID to the history request.
    cancelable_consumer_.SetClientData(hs, handle, request_id);
  } else {
    // Tell the caller that no thumbnail is available.
    SendResponse(request_id, NULL);
  }
}

std::string DOMUIThumbnailSource::GetMimeType(const std::string&) const {
  // We need to explicitly return a mime type, otherwise if the user tries to
  // drag the image they get no extension.
  return "image/png";
}

MessageLoop* DOMUIThumbnailSource::MessageLoopForRequestPath(
    const std::string& path) const {
  // TopSites can be accessed from the IO thread.
  return top_sites_.get() ? NULL : DataSource::MessageLoopForRequestPath(path);
}

void DOMUIThumbnailSource::SendDefaultThumbnail(int request_id) {
  // Use placeholder thumbnail.
  if (!default_thumbnail_.get()) {
    default_thumbnail_ =
        ResourceBundle::GetSharedInstance().LoadDataResourceBytes(
            IDR_DEFAULT_THUMBNAIL);
  }
  SendResponse(request_id, default_thumbnail_);
}

void DOMUIThumbnailSource::OnThumbnailDataAvailable(
    HistoryService::Handle request_handle,
    scoped_refptr<RefCountedBytes> data) {
  int request_id = cancelable_consumer_.GetClientDataForCurrentRequest();
  // Forward the data along to the networking system.
  if (data.get() && !data->data.empty()) {
    SendResponse(request_id, data);
  } else {
    SendDefaultThumbnail(request_id);
  }
}
