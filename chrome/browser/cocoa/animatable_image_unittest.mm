// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <Cocoa/Cocoa.h>

#import "base/nsimage_cache_mac.h"
#import "chrome/browser/cocoa/animatable_image.h"
#import "chrome/browser/cocoa/cocoa_test_helper.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/platform_test.h"

namespace {

class AnimatableImageTest : public CocoaTest {
 public:
  AnimatableImageTest() {
    NSRect frame = NSMakeRect(0, 0, 500, 500);
    NSImage* image = nsimage_cache::ImageNamed(@"forward_Template.pdf");
    animation_ = [[AnimatableImage alloc] initWithImage:image
                                         animationFrame:frame];
  }

  AnimatableImage* animation_;
};

TEST_F(AnimatableImageTest, BasicAnimation) {
  [animation_ setStartFrame:CGRectMake(0, 0, 10, 10)];
  [animation_ setEndFrame:CGRectMake(500, 500, 100, 100)];
  [animation_ setStartOpacity:0.1];
  [animation_ setEndOpacity:1.0];
  [animation_ setDuration:0.5];
  [animation_ startAnimation];
}

}  // namespace
