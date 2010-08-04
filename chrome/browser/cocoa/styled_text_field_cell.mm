// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/cocoa/styled_text_field_cell.h"

#include "app/resource_bundle.h"
#include "base/logging.h"
#include "chrome/browser/browser_theme_provider.h"
#import "chrome/browser/cocoa/themed_window.h"
#include "gfx/font.h"
#include "grit/theme_resources.h"

namespace {

NSBezierPath* RectPathWithInset(const NSRect frame,
                                const CGFloat inset,
                                const CGFloat outerRadius) {
  const NSRect insetFrame = NSInsetRect(frame, inset, inset);
  if (outerRadius > 0.0) {
    return [NSBezierPath bezierPathWithRoundedRect:insetFrame
                                           xRadius:outerRadius - inset
                                           yRadius:outerRadius - inset];
  } else {
    return [NSBezierPath bezierPathWithRect:insetFrame];
  }
}

// Similar to |NSRectFill()|, additionally sets |color| as the fill
// color.  |outerRadius| greater than 0.0 uses rounded corners, with
// inset backed out of the radius.
void FillRectWithInset(const NSRect frame,
                       const CGFloat inset,
                       const CGFloat outerRadius,
                       NSColor* color) {
  NSBezierPath* path = RectPathWithInset(frame, inset, outerRadius);
  [color setFill];
  [path fill];
}

// Similar to |NSFrameRectWithWidth()|, additionally sets |color| as
// the stroke color (as opposed to the fill color).  |outerRadius|
// greater than 0.0 uses rounded corners, with inset backed out of the
// radius.
void FrameRectWithInset(const NSRect frame,
                        const CGFloat inset,
                        const CGFloat outerRadius,
                        const CGFloat lineWidth,
                        NSColor* color) {
  const CGFloat finalInset = inset + (lineWidth / 2.0);
  NSBezierPath* path = RectPathWithInset(frame, finalInset, outerRadius);
  [color setStroke];
  [path setLineWidth:lineWidth];
  [path stroke];
}

// TODO(shess): Maybe we need a |cocoa_util.h|?
class ScopedSaveGraphicsState {
 public:
  ScopedSaveGraphicsState()
      : context_([NSGraphicsContext currentContext]) {
    [context_ saveGraphicsState];
  }
  explicit ScopedSaveGraphicsState(NSGraphicsContext* context)
      : context_(context) {
    [context_ saveGraphicsState];
  }
  ~ScopedSaveGraphicsState() {
    [context_ restoreGraphicsState];
  }

private:
  NSGraphicsContext* context_;
};

}  // namespace

@implementation StyledTextFieldCell

- (CGFloat)baselineAdjust {
  return 0.0;
}

- (CGFloat)cornerRadius {
  return 0.0;
}

- (BOOL)shouldDrawBezel {
  return NO;
}

// Returns the same value as textCursorFrameForFrame, but does not call it
// directly to avoid potential infinite loops.
- (NSRect)textFrameForFrame:(NSRect)cellFrame {
  return NSInsetRect(cellFrame, 0, [self baselineAdjust]);
}

// Returns the same value as textFrameForFrame, but does not call it directly to
// avoid potential infinite loops.
- (NSRect)textCursorFrameForFrame:(NSRect)cellFrame {
  return NSInsetRect(cellFrame, 0, [self baselineAdjust]);
}

// Override to show the I-beam cursor only in the area given by
// |textCursorFrameForFrame:|.
- (void)resetCursorRect:(NSRect)cellFrame inView:(NSView *)controlView {
  [super resetCursorRect:[self textCursorFrameForFrame:cellFrame]
                  inView:controlView];
}

// For NSTextFieldCell this is the area within the borders.  For our
// purposes, we count the info decorations as being part of the
// border.
- (NSRect)drawingRectForBounds:(NSRect)theRect {
  return [super drawingRectForBounds:[self textFrameForFrame:theRect]];
}

// TODO(shess): This code is manually drawing the cell's border area,
// but otherwise the cell assumes -setBordered:YES for purposes of
// calculating things like the editing area.  This is probably
// incorrect.  I know that this affects -drawingRectForBounds:.
- (void)drawWithFrame:(NSRect)cellFrame inView:(NSView*)controlView {
  DCHECK([controlView isFlipped]);

  // TODO(shess): This inset is also reflected by |kFieldVisualInset|
  // in autocomplete_popup_view_mac.mm.
  const NSRect frame = NSInsetRect(cellFrame, 0, 1);
  const CGFloat radius = [self cornerRadius];

  // Paint button background image if there is one (otherwise the border won't
  // look right).
  ThemeProvider* themeProvider = [[controlView window] themeProvider];
  if (themeProvider) {
    NSColor* backgroundImageColor =
        themeProvider->GetNSImageColorNamed(IDR_THEME_BUTTON_BACKGROUND, false);
    if (backgroundImageColor) {
      // Set the phase to match window.
      NSRect trueRect = [controlView convertRect:cellFrame toView:nil];
      NSPoint midPoint = NSMakePoint(NSMinX(trueRect), NSMaxY(trueRect));
      [[NSGraphicsContext currentContext] setPatternPhase:midPoint];

      // NOTE(shess): This seems like it should be using a 0.0 inset,
      // but AFAICT using a 0.5 inset is important in mixing the
      // toolbar background and the omnibox background.
      FillRectWithInset(frame, 0.5, radius, backgroundImageColor);
    }

    // Draw the outer stroke (over the background).
    BOOL active = [[controlView window] isMainWindow];
    NSColor* strokeColor = themeProvider->GetNSColor(
        active ? BrowserThemeProvider::COLOR_TOOLBAR_BUTTON_STROKE :
                 BrowserThemeProvider::COLOR_TOOLBAR_BUTTON_STROKE_INACTIVE,
        true);
    FrameRectWithInset(frame, 0.0, radius, 1.0, strokeColor);
  }

  // Fill interior with background color.
  FillRectWithInset(frame, 1.0, radius, [self backgroundColor]);

  // Draw the shadow.  For the rounded-rect case, the shadow needs to
  // slightly turn in at the corners.  |shadowFrame| is at the same
  // midline as the inner border line on the top and left, but at the
  // outer border line on the bottom and right.  The clipping change
  // will clip the bottom and right edges (and corner).
  {
    ScopedSaveGraphicsState state;
    [RectPathWithInset(frame, 1.0, radius) addClip];
    const NSRect shadowFrame = NSOffsetRect(frame, 0.5, 0.5);
    NSColor* shadowShade = [NSColor colorWithCalibratedWhite:0.0 alpha:0.05];
    FrameRectWithInset(shadowFrame, 0.5, radius - 0.5, 1.0, shadowShade);
  }

  // Draw optional bezel below bottom stroke.
  if ([self shouldDrawBezel]) {
    [[NSColor colorWithCalibratedWhite:0.96 alpha:1.0] set];
    NSRect bezelRect = NSMakeRect(cellFrame.origin.x,
                                  NSMaxY(cellFrame) - 0.5,
                                  NSWidth(cellFrame),
                                  1.0);
    bezelRect = NSInsetRect(bezelRect, radius - 0.5, 0.0);
    NSRectFill(bezelRect);
  }

  // Draw the focus ring if needed.
  if ([self showsFirstResponder]) {
    NSColor* color =
        [[NSColor keyboardFocusIndicatorColor] colorWithAlphaComponent:0.5];
    FrameRectWithInset(frame, 0.0, radius, 2.0, color);
  }

  [self drawInteriorWithFrame:cellFrame inView:controlView];
}

@end
