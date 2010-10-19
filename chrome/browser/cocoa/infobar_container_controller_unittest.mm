// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <Cocoa/Cocoa.h>

#include "base/scoped_nsobject.h"
#import "chrome/browser/cocoa/cocoa_test_helper.h"
#import "chrome/browser/cocoa/infobar_container_controller.h"
#include "chrome/browser/cocoa/infobar_test_helper.h"
#import "chrome/browser/cocoa/view_resizer_pong.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/platform_test.h"

namespace {

class InfoBarContainerControllerTest : public CocoaTest {
  virtual void SetUp() {
    CocoaTest::SetUp();
    resizeDelegate_.reset([[ViewResizerPong alloc] init]);
    ViewResizerPong *viewResizer = resizeDelegate_.get();
    controller_ =
        [[InfoBarContainerController alloc] initWithResizeDelegate:viewResizer];
    NSView* view = [controller_ view];
    [[test_window() contentView] addSubview:view];
  }

  virtual void TearDown() {
    [[controller_ view] removeFromSuperviewWithoutNeedingDisplay];
    [controller_ release];
    CocoaTest::TearDown();
  }

 public:
  scoped_nsobject<ViewResizerPong> resizeDelegate_;
  InfoBarContainerController* controller_;
};

TEST_VIEW(InfoBarContainerControllerTest, [controller_ view])

TEST_F(InfoBarContainerControllerTest, BWCPong) {
  // Call positionInfoBarsAndResize and check that |resizeDelegate_| got a
  // resize message.
  [resizeDelegate_ setHeight:-1];
  [controller_ positionInfoBarsAndRedraw];
  EXPECT_NE(-1, [resizeDelegate_ height]);
}

TEST_F(InfoBarContainerControllerTest, AddAndRemoveInfoBars) {
  NSView* view = [controller_ view];

  // Add three infobars, one of each type, and then remove them.
  // After each step check to make sure we have the correct number of
  // infobar subviews.
  MockAlertInfoBarDelegate alertDelegate;
  MockLinkInfoBarDelegate linkDelegate;
  MockConfirmInfoBarDelegate confirmDelegate;

  [controller_ addInfoBar:&alertDelegate animate:NO];
  EXPECT_EQ(1U, [[view subviews] count]);

  [controller_ addInfoBar:&linkDelegate animate:NO];
  EXPECT_EQ(2U, [[view subviews] count]);

  [controller_ addInfoBar:&confirmDelegate animate:NO];
  EXPECT_EQ(3U, [[view subviews] count]);

  // Just to mix things up, remove them in a different order.
  [controller_ closeInfoBarsForDelegate:&linkDelegate animate:NO];
  EXPECT_EQ(2U, [[view subviews] count]);

  [controller_ closeInfoBarsForDelegate:&confirmDelegate animate:NO];
  EXPECT_EQ(1U, [[view subviews] count]);

  [controller_ closeInfoBarsForDelegate:&alertDelegate animate:NO];
  EXPECT_EQ(0U, [[view subviews] count]);
}

TEST_F(InfoBarContainerControllerTest, RemoveAllInfoBars) {
  NSView* view = [controller_ view];

  // Add three infobars and then remove them all.
  MockAlertInfoBarDelegate alertDelegate;
  MockLinkInfoBarDelegate linkDelegate;
  MockConfirmInfoBarDelegate confirmDelegate;

  [controller_ addInfoBar:&alertDelegate animate:NO];
  [controller_ addInfoBar:&linkDelegate animate:NO];
  [controller_ addInfoBar:&confirmDelegate animate:NO];
  EXPECT_EQ(3U, [[view subviews] count]);

  [controller_ removeAllInfoBars];
  EXPECT_EQ(0U, [[view subviews] count]);
}
}  // namespace
