// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "base/scoped_nsobject.h"
#include "chrome/app/chrome_dll_resource.h"
#import "chrome/browser/cocoa/chrome_event_processing_window.h"
#import "chrome/browser/cocoa/browser_window_controller.h"
#import "chrome/browser/cocoa/browser_frame_view.h"
#import "chrome/browser/cocoa/cocoa_test_helper.h"
#import "third_party/ocmock/OCMock/OCMock.h"

namespace {

NSEvent* KeyEvent(const NSUInteger flags, const NSUInteger keyCode) {
  return [NSEvent keyEventWithType:NSKeyDown
                          location:NSZeroPoint
                     modifierFlags:flags
                         timestamp:0.0
                      windowNumber:0
                           context:nil
                        characters:@""
       charactersIgnoringModifiers:@""
                         isARepeat:NO
                          keyCode:keyCode];
}

class ChromeEventProcessingWindowTest : public CocoaTest {
 public:
  virtual void SetUp() {
    CocoaTest::SetUp();
    // Create a window.
    const NSUInteger mask = NSTitledWindowMask | NSClosableWindowMask |
        NSMiniaturizableWindowMask | NSResizableWindowMask;
    window_ = [[ChromeEventProcessingWindow alloc]
               initWithContentRect:NSMakeRect(0, 0, 800, 600)
                         styleMask:mask
                           backing:NSBackingStoreBuffered
                             defer:NO];
    if (DebugUtil::BeingDebugged()) {
      [window_ orderFront:nil];
    } else {
      [window_ orderBack:nil];
    }
  }

  virtual void TearDown() {
    [window_ close];
    CocoaTest::TearDown();
  }

  // Returns a canonical snapshot of the window.
  NSData* WindowContentsAsTIFF() {
    NSRect frame([window_ frame]);
    frame.origin = [window_ convertScreenToBase:frame.origin];

    NSData* pdfData = [window_ dataWithPDFInsideRect:frame];

    // |pdfData| can differ for windows which look the same, so make it
    // canonical.
    NSImage* image = [[[NSImage alloc] initWithData:pdfData] autorelease];
    return [image TIFFRepresentation];
  }

  ChromeEventProcessingWindow* window_;
};

id CreateBrowserWindowControllerMock() {
  id delegate = [OCMockObject mockForClass:[BrowserWindowController class]];
  // Make conformsToProtocol return YES for @protocol(BrowserCommandExecutor)
  // to satisfy the DCHECK() in handleExtraKeyboardShortcut.
  //
  // TODO(akalin): Figure out how to replace OCMOCK_ANY below with
  // @protocol(BrowserCommandExecutor) and have it work.
  BOOL yes = YES;
  [[[delegate stub] andReturnValue:OCMOCK_VALUE(yes)]
    conformsToProtocol:OCMOCK_ANY];
  return delegate;
}

// Verify that the window intercepts a particular key event and
// forwards it to [delegate executeCommand:].  Assume that other
// CommandForKeyboardShortcut() will work the same for the rest.
TEST_F(ChromeEventProcessingWindowTest,
       PerformKeyEquivalentForwardToExecuteCommand) {
  NSEvent* event = KeyEvent(NSCommandKeyMask, kVK_ANSI_1);

  id delegate = CreateBrowserWindowControllerMock();
  [[delegate expect] executeCommand:IDC_SELECT_TAB_0];

  [window_ setDelegate:delegate];
  [window_ performKeyEquivalent:event];

  // Don't wish to mock all the way down...
  [window_ setDelegate:nil];
  [delegate verify];
}

// Verify that an unhandled shortcut does not get forwarded via
// -executeCommand:.
// TODO(shess) Think of a way to test that it is sent to the
// superclass.
TEST_F(ChromeEventProcessingWindowTest, PerformKeyEquivalentNoForward) {
  NSEvent* event = KeyEvent(0, 0);

  id delegate = CreateBrowserWindowControllerMock();

  [window_ setDelegate:delegate];
  [window_ performKeyEquivalent:event];

  // Don't wish to mock all the way down...
  [window_ setDelegate:nil];
  [delegate verify];
}

}  // namespace
