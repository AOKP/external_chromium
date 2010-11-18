// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "speech_input_window_controller.h"

#include "app/l10n_util_mac.h"
#include "app/resource_bundle.h"
#include "base/logging.h"
#include "base/mac_util.h"
#include "base/sys_string_conversions.h"

#include "chrome/browser/cocoa/info_bubble_view.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#import "skia/ext/skia_utils_mac.h"
#import "third_party/GTM/AppKit/GTMUILocalizerAndLayoutTweaker.h"

const int kBubbleControlVerticalSpacing = 10;  // Space between controls.
const int kBubbleHorizontalMargin = 5;  // Space on either sides of controls.

@interface SpeechInputWindowController (Private)
- (NSSize)calculateContentSize;
- (void)layout:(NSSize)size;
@end

@implementation SpeechInputWindowController

- (id)initWithParentWindow:(NSWindow*)parentWindow
                  delegate:(SpeechInputBubbleDelegate*)delegate
              anchoredAt:(NSPoint)anchoredAt {
  anchoredAt.y += info_bubble::kBubbleArrowHeight / 2.0;
  if ((self = [super initWithWindowNibPath:@"SpeechInputBubble"
                              parentWindow:parentWindow
                                anchoredAt:anchoredAt])) {
    DCHECK(delegate);
    delegate_ = delegate;

    [self showWindow:nil];
  }
  return self;
}

- (void)awakeFromNib {
  [super awakeFromNib];

  NSWindow* window = [self window];
  [[self bubble] setArrowLocation:info_bubble::kTopLeft];

  NSSize newSize = [self calculateContentSize];
  [[self bubble] setFrameSize:newSize];
  NSSize windowDelta = NSMakeSize(
      newSize.width - NSWidth([[window contentView] bounds]),
      newSize.height - NSHeight([[window contentView] bounds]));
  windowDelta = [[window contentView] convertSize:windowDelta toView:nil];
  NSRect newFrame = [window frame];
  newFrame.size.width += windowDelta.width;
  newFrame.size.height += windowDelta.height;
  [window setFrame:newFrame display:NO];

  [self layout:newSize];  // Layout all the child controls.
}

- (IBAction)cancel:(id)sender {
  delegate_->InfoBubbleButtonClicked(SpeechInputBubble::BUTTON_CANCEL);
}

- (IBAction)tryAgain:(id)sender {
  delegate_->InfoBubbleButtonClicked(SpeechInputBubble::BUTTON_TRY_AGAIN);
}

// Calculate the window dimensions to reflect the sum height and max width of
// all controls, with appropriate spacing between and around them. The returned
// size is in view coordinates.
- (NSSize)calculateContentSize {
  [GTMUILocalizerAndLayoutTweaker sizeToFitView:cancelButton_];
  [GTMUILocalizerAndLayoutTweaker sizeToFitView:tryAgainButton_];
  NSSize cancelSize = [cancelButton_ bounds].size;
  NSSize tryAgainSize = [tryAgainButton_ bounds].size;
  int newHeight = cancelSize.height + kBubbleControlVerticalSpacing;
  int newWidth = cancelSize.width + tryAgainSize.width;

  if (![iconImage_ isHidden]) {
    NSImage* icon = ResourceBundle::GetSharedInstance().GetNativeImageNamed(
        IDR_SPEECH_INPUT_MIC_EMPTY);
    NSSize size = [icon size];
    newHeight += size.height + kBubbleControlVerticalSpacing;
    if (newWidth < size.width)
      newWidth = size.width;
  } else {
    newHeight += kBubbleControlVerticalSpacing;
  }

  [GTMUILocalizerAndLayoutTweaker sizeToFitFixedWidthTextField:
      instructionLabel_];
  NSSize size = [instructionLabel_ bounds].size;
  newHeight += size.height;
  if (newWidth < size.width)
    newWidth = size.width;

  return NSMakeSize(newWidth + 2 * kBubbleHorizontalMargin,
                    newHeight + 2 * kBubbleControlVerticalSpacing);
}

// Position the controls within the given content area bounds.
- (void)layout:(NSSize)size {
  int y = kBubbleControlVerticalSpacing;

  NSRect cancelRect = [cancelButton_ bounds];

  if ([tryAgainButton_ isHidden]) {
    cancelRect.origin.x = (size.width - NSWidth(cancelRect)) / 2;
  } else {
    NSRect tryAgainRect = [tryAgainButton_ bounds];
    cancelRect.origin.x = (size.width - NSWidth(cancelRect) -
                           NSWidth(tryAgainRect)) / 2;
    tryAgainRect.origin.x = cancelRect.origin.x + NSWidth(cancelRect);
    tryAgainRect.origin.y = y;
    [tryAgainButton_ setFrame:tryAgainRect];
  }
  cancelRect.origin.y = y;
  [cancelButton_ setFrame:cancelRect];

  y += NSHeight(cancelRect) + kBubbleControlVerticalSpacing;

  NSRect rect;
  if (![iconImage_ isHidden]) {
    rect = [iconImage_ bounds];
    rect.origin.x = (size.width - NSWidth(rect)) / 2;
    rect.origin.y = y;
    [iconImage_ setFrame:rect];
    y += rect.size.height + kBubbleControlVerticalSpacing;
  }

  rect = [instructionLabel_ bounds];
  rect.origin.x = (size.width - NSWidth(rect)) / 2;
  rect.origin.y = y;
  [instructionLabel_ setFrame:rect];
}

- (void)updateLayout:(SpeechInputBubbleBase::DisplayMode)mode
         messageText:(const string16&)messageText {
  // Get the right set of controls to be visible.
  if (mode == SpeechInputBubbleBase::DISPLAY_MODE_MESSAGE) {
    [instructionLabel_ setStringValue:base::SysUTF16ToNSString(messageText)];
    [iconImage_ setHidden:YES];
    [tryAgainButton_ setHidden:NO];
  } else {
    [instructionLabel_ setStringValue:l10n_util::GetNSString(
        IDS_SPEECH_INPUT_BUBBLE_HEADING)];
    if (mode == SpeechInputBubbleBase::DISPLAY_MODE_RECORDING) {
      NSImage* icon = ResourceBundle::GetSharedInstance().GetNativeImageNamed(
          IDR_SPEECH_INPUT_MIC_EMPTY);
      [iconImage_ setImage:icon];
    }
    [iconImage_ setHidden:NO];
    [iconImage_ setNeedsDisplay:YES];
    [tryAgainButton_ setHidden:YES];
  }

  NSSize newSize = [self calculateContentSize];
  NSRect rect = [[self bubble] frame];
  rect.origin.y -= newSize.height - rect.size.height;
  rect.size = newSize;
  [[self bubble] setFrame:rect];
  [self layout:newSize];
}

- (void)windowWillClose:(NSNotification*)notification {
  delegate_->InfoBubbleFocusChanged();
}

- (void)show {
  [self showWindow:nil];
}

- (void)hide {
  [[self window] orderOut:nil];
}

- (void)setImage:(NSImage*)image {
  [iconImage_ setImage:image];
}

@end  // implementation SpeechInputWindowController
