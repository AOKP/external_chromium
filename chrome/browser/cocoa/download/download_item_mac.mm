// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/cocoa/download/download_item_mac.h"

#include "base/callback.h"
#include "chrome/browser/browser_process.h"
#import "chrome/browser/cocoa/download/download_item_controller.h"
#include "chrome/browser/cocoa/download/download_util_mac.h"
#include "chrome/browser/download/download_item.h"
#include "chrome/browser/download/download_item_model.h"
#include "skia/ext/skia_utils_mac.h"

// DownloadItemMac -------------------------------------------------------------

DownloadItemMac::DownloadItemMac(BaseDownloadItemModel* download_model,
                                 DownloadItemController* controller)
    : download_model_(download_model), item_controller_(controller) {
  download_model_->download()->AddObserver(this);
}

DownloadItemMac::~DownloadItemMac() {
  download_model_->download()->RemoveObserver(this);
  icon_consumer_.CancelAllRequests();
}

void DownloadItemMac::OnDownloadUpdated(DownloadItem* download) {
  DCHECK_EQ(download, download_model_->download());

  if ([item_controller_ isDangerousMode] &&
      download->safety_state() == DownloadItem::DANGEROUS_BUT_VALIDATED) {
    // We have been approved.
    [item_controller_ clearDangerousMode];
  }

  if (download->full_path() != lastFilePath_) {
    // Turns out the file path is "unconfirmed %d.download" for dangerous
    // downloads. When the download is confirmed, the file is renamed on
    // another thread, so reload the icon if the download filename changes.
    LoadIcon();
    lastFilePath_ = download->full_path();

    [item_controller_ updateToolTip];
  }

  switch (download->state()) {
    case DownloadItem::REMOVING:
      [item_controller_ remove];  // We're deleted now!
      break;
    case DownloadItem::COMPLETE:
      if (download->auto_opened()) {
        [item_controller_ remove];  // We're deleted now!
        return;
      }
      download_util::NotifySystemOfDownloadComplete(download->full_path());
      // fall through
    case DownloadItem::IN_PROGRESS:
    case DownloadItem::CANCELLED:
      [item_controller_ setStateFromDownload:download_model_.get()];
      break;
    default:
      NOTREACHED();
  }
}

void DownloadItemMac::LoadIcon() {
  IconManager* icon_manager = g_browser_process->icon_manager();
  if (!icon_manager) {
    NOTREACHED();
    return;
  }

  // We may already have this particular image cached.
  FilePath file = download_model_->download()->full_path();
  SkBitmap* icon_bitmap = icon_manager->LookupIcon(file, IconLoader::SMALL);
  if (icon_bitmap) {
    NSImage* icon = gfx::SkBitmapToNSImage(*icon_bitmap);
    [item_controller_ setIcon:icon];
    return;
  }

  // The icon isn't cached, load it asynchronously.
  icon_manager->LoadIcon(file, IconLoader::SMALL, &icon_consumer_,
                         NewCallback(this,
                                     &DownloadItemMac::OnExtractIconComplete));
}

void DownloadItemMac::OnExtractIconComplete(IconManager::Handle handle,
                                            SkBitmap* icon_bitmap) {
  if (!icon_bitmap)
    return;

  NSImage* icon = gfx::SkBitmapToNSImage(*icon_bitmap);
  [item_controller_ setIcon:icon];
}
