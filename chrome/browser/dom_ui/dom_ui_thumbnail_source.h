// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DOM_UI_DOM_UI_THUMBNAIL_SOURCE_H_
#define CHROME_BROWSER_DOM_UI_DOM_UI_THUMBNAIL_SOURCE_H_
#pragma once

#include <string>

#include "base/basictypes.h"
#include "base/ref_counted.h"
#include "chrome/browser/dom_ui/chrome_url_data_manager.h"
#include "chrome/browser/history/history.h"
#include "chrome/common/notification_registrar.h"

class Profile;

namespace history {
class TopSites;
}

// ThumbnailSource is the gateway between network-level chrome: requests for
// thumbnails and the history/top-sites backend that serves these.
class DOMUIThumbnailSource : public ChromeURLDataManager::DataSource {
 public:
  explicit DOMUIThumbnailSource(Profile* profile);

  // Called when the network layer has requested a resource underneath
  // the path we registered.
  virtual void StartDataRequest(const std::string& path,
                                bool is_off_the_record,
                                int request_id);

  virtual std::string GetMimeType(const std::string& path) const;

  virtual MessageLoop* MessageLoopForRequestPath(const std::string& path) const;

  // Called when thumbnail data is available from the history backend.
  void OnThumbnailDataAvailable(HistoryService::Handle request_handle,
                                scoped_refptr<RefCountedBytes> data);

 private:
  virtual ~DOMUIThumbnailSource();

  // Send the default thumbnail when we are missing a real one.
  void SendDefaultThumbnail(int request_id);

  Profile* profile_;

  CancelableRequestConsumerT<int, 0> cancelable_consumer_;

  // Raw PNG representation of the thumbnail to show when the thumbnail
  // database doesn't have a thumbnail for a webpage.
  scoped_refptr<RefCountedMemory> default_thumbnail_;

  // TopSites. If non-null we're using TopSites.
  scoped_refptr<history::TopSites> top_sites_;

  DISALLOW_COPY_AND_ASSIGN(DOMUIThumbnailSource);
};

#endif  // CHROME_BROWSER_DOM_UI_DOM_UI_THUMBNAIL_SOURCE_H_
