// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <Cocoa/Cocoa.h>

#import "chrome/browser/cocoa/reload_button.h"

#include "base/scoped_nsobject.h"
#include "chrome/app/chrome_dll_resource.h"
#import "chrome/browser/cocoa/cocoa_test_helper.h"
#import "chrome/browser/cocoa/test_event_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/platform_test.h"
#import "third_party/ocmock/OCMock/OCMock.h"

@protocol TargetActionMock <NSObject>
- (void)anAction:(id)sender;
@end

namespace {

class ReloadButtonTest : public CocoaTest {
 public:
  ReloadButtonTest() {
    NSRect frame = NSMakeRect(0, 0, 20, 20);
    scoped_nsobject<ReloadButton> button(
        [[ReloadButton alloc] initWithFrame:frame]);
    button_ = button.get();

    // Set things up so unit tests have a reliable baseline.
    [button_ setTag:IDC_RELOAD];
    [button_ awakeFromNib];

    [[test_window() contentView] addSubview:button_];
  }

  ReloadButton* button_;
};

TEST_VIEW(ReloadButtonTest, button_)

// Test that mouse-tracking is setup and does the right thing.
TEST_F(ReloadButtonTest, IsMouseInside) {
  EXPECT_TRUE([[button_ trackingAreas] containsObject:[button_ trackingArea]]);

  EXPECT_FALSE([button_ isMouseInside]);
  [button_ mouseEntered:nil];
  EXPECT_TRUE([button_ isMouseInside]);
  [button_ mouseExited:nil];
}

// Verify that multiple clicks do not result in multiple messages to
// the target.
TEST_F(ReloadButtonTest, IgnoredMultiClick) {
  id mock_target = [OCMockObject mockForProtocol:@protocol(TargetActionMock)];
  [button_ setTarget:mock_target];
  [button_ setAction:@selector(anAction:)];

  // Expect the action once.
  [[mock_target expect] anAction:button_];

  const std::pair<NSEvent*,NSEvent*> click_one =
      test_event_utils::MouseClickInView(button_, 1);
  const std::pair<NSEvent*,NSEvent*> click_two =
      test_event_utils::MouseClickInView(button_, 2);
  [NSApp postEvent:click_one.second atStart:YES];
  [button_ mouseDown:click_one.first];
  [NSApp postEvent:click_two.second atStart:YES];
  [button_ mouseDown:click_two.first];

  [button_ setTarget:nil];
}

// Test that when forcing the mode, it takes effect immediately,
// regardless of whether the mouse is hovering.
TEST_F(ReloadButtonTest, SetIsLoadingForce) {
  EXPECT_FALSE([button_ isMouseInside]);
  EXPECT_EQ([button_ tag], IDC_RELOAD);

  // Changes to stop immediately.
  [button_ setIsLoading:YES force:YES];
  EXPECT_EQ([button_ tag], IDC_STOP);

  // Changes to reload immediately.
  [button_ setIsLoading:NO force:YES];
  EXPECT_EQ([button_ tag], IDC_RELOAD);

  // Changes to stop immediately when the mouse is hovered, and
  // doesn't change when the mouse exits.
  [button_ mouseEntered:nil];
  EXPECT_TRUE([button_ isMouseInside]);
  [button_ setIsLoading:YES force:YES];
  EXPECT_EQ([button_ tag], IDC_STOP);
  [button_ mouseExited:nil];
  EXPECT_FALSE([button_ isMouseInside]);
  EXPECT_EQ([button_ tag], IDC_STOP);

  // Changes to reload immediately when the mouse is hovered, and
  // doesn't change when the mouse exits.
  [button_ mouseEntered:nil];
  EXPECT_TRUE([button_ isMouseInside]);
  [button_ setIsLoading:NO force:YES];
  EXPECT_EQ([button_ tag], IDC_RELOAD);
  [button_ mouseExited:nil];
  EXPECT_FALSE([button_ isMouseInside]);
  EXPECT_EQ([button_ tag], IDC_RELOAD);
}

// Test that without force, stop mode is set immediately, but reload
// is affected by the hover status.
TEST_F(ReloadButtonTest, SetIsLoadingNoForce) {
  EXPECT_FALSE([button_ isMouseInside]);
  EXPECT_EQ([button_ tag], IDC_RELOAD);

  // Changes to stop immediately when the mouse is not hovering.
  [button_ setIsLoading:YES force:NO];
  EXPECT_EQ([button_ tag], IDC_STOP);

  // Changes to reload immediately when the mouse is not hovering.
  [button_ setIsLoading:NO force:NO];
  EXPECT_EQ([button_ tag], IDC_RELOAD);

  // Changes to stop immediately when the mouse is hovered, and
  // doesn't change when the mouse exits.
  [button_ mouseEntered:nil];
  EXPECT_TRUE([button_ isMouseInside]);
  [button_ setIsLoading:YES force:NO];
  EXPECT_EQ([button_ tag], IDC_STOP);
  [button_ mouseExited:nil];
  EXPECT_FALSE([button_ isMouseInside]);
  EXPECT_EQ([button_ tag], IDC_STOP);

  // Does not change to reload immediately when the mouse is hovered,
  // changes when the mouse exits.
  [button_ mouseEntered:nil];
  EXPECT_TRUE([button_ isMouseInside]);
  [button_ setIsLoading:NO force:NO];
  EXPECT_EQ([button_ tag], IDC_STOP);
  [button_ mouseExited:nil];
  EXPECT_FALSE([button_ isMouseInside]);
  EXPECT_EQ([button_ tag], IDC_RELOAD);
}

// Test that pressing stop after reload mode has been requested
// doesn't forward the stop message.
TEST_F(ReloadButtonTest, StopAfterReloadSet) {
  id mock_target = [OCMockObject mockForProtocol:@protocol(TargetActionMock)];
  [button_ setTarget:mock_target];
  [button_ setAction:@selector(anAction:)];

  EXPECT_FALSE([button_ isMouseInside]);

  // Get to stop mode.
  [button_ setIsLoading:YES force:YES];
  EXPECT_EQ([button_ tag], IDC_STOP);
  EXPECT_TRUE([button_ isEnabled]);

  // Expect the action once.
  [[mock_target expect] anAction:button_];

  // Clicking in stop mode should send the action and transition to
  // reload mode.
  const std::pair<NSEvent*,NSEvent*> click =
      test_event_utils::MouseClickInView(button_, 1);
  [NSApp postEvent:click.second atStart:YES];
  [button_ mouseDown:click.first];
  EXPECT_EQ([button_ tag], IDC_RELOAD);
  EXPECT_TRUE([button_ isEnabled]);

  // Get back to stop mode.
  [button_ setIsLoading:YES force:YES];
  EXPECT_EQ([button_ tag], IDC_STOP);
  EXPECT_TRUE([button_ isEnabled]);

  // If hover prevented reload mode immediately taking effect, clicks should do
  // nothing, because the button should be disabled.
  [button_ mouseEntered:nil];
  EXPECT_TRUE([button_ isMouseInside]);
  [button_ setIsLoading:NO force:NO];
  EXPECT_EQ([button_ tag], IDC_STOP);
  EXPECT_FALSE([button_ isEnabled]);
  [NSApp postEvent:click.second atStart:YES];
  [button_ mouseDown:click.first];
  EXPECT_EQ([button_ tag], IDC_STOP);

  [button_ setTarget:nil];
}

}  // namespace
