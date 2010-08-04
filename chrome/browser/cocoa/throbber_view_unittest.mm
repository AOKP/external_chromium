// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <Cocoa/Cocoa.h>

#include "app/resource_bundle.h"
#include "base/scoped_nsobject.h"
#import "chrome/browser/cocoa/throbber_view.h"
#import "chrome/browser/cocoa/cocoa_test_helper.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/platform_test.h"
#include "grit/app_resources.h"

namespace {

class ThrobberViewTest : public CocoaTest {
 public:
  ThrobberViewTest() {
    NSRect frame = NSMakeRect(10, 10, 16, 16);
    NSImage* image =
        ResourceBundle::GetSharedInstance().GetNSImageNamed(IDR_THROBBER);
    view_ = [ThrobberView filmstripThrobberViewWithFrame:frame image:image];
    [[test_window() contentView] addSubview:view_];
  }

  ThrobberView* view_;
};

TEST_VIEW(ThrobberViewTest, view_)

}  // namespace
