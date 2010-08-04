// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/notifications/balloon_collection_impl.h"

#include "chrome/browser/cocoa/notifications/balloon_view_bridge.h"

Balloon* BalloonCollectionImpl::MakeBalloon(const Notification& notification,
                                            Profile* profile) {
  Balloon* balloon = new Balloon(notification, profile, this);
  balloon->set_view(new BalloonViewBridge());
  gfx::Size size(layout_.min_balloon_width(), layout_.min_balloon_height());
  balloon->set_content_size(size);
  return balloon;
}

// static
gfx::Rect BalloonCollectionImpl::GetMacWorkArea() {
  NSScreen* primary = [[NSScreen screens] objectAtIndex:0];
  return gfx::Rect(NSRectToCGRect([primary visibleFrame]));
}

int BalloonCollectionImpl::Layout::InterBalloonMargin() const {
  return 5;
}

int BalloonCollectionImpl::Layout::HorizontalEdgeMargin() const {
  return 5;
}

int BalloonCollectionImpl::Layout::VerticalEdgeMargin() const {
  return 5;
}

// static
BalloonCollection* BalloonCollection::Create() {
  return new BalloonCollectionImpl();
}
