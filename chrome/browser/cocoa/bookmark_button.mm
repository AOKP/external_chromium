// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/cocoa/bookmark_button.h"
#include "base/logging.h"
#import "base/scoped_nsobject.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#import "chrome/browser/cocoa/bookmark_button_cell.h"
#import "chrome/browser/cocoa/browser_window_controller.h"
#import "chrome/browser/cocoa/view_id_util.h"
#include "chrome/browser/metrics/user_metrics.h"

// The opacity of the bookmark button drag image.
static const CGFloat kDragImageOpacity = 0.7;


namespace bookmark_button {

NSString* const kPulseBookmarkButtonNotification =
    @"PulseBookmarkButtonNotification";
NSString* const kBookmarkKey = @"BookmarkKey";
NSString* const kBookmarkPulseFlagKey = @"BookmarkPulseFlagKey";

};

@interface BookmarkButton(Private)

// Make a drag image for the button.
- (NSImage*)dragImage;

@end  // @interface BookmarkButton(Private)


@implementation BookmarkButton

@synthesize delegate = delegate_;

- (id)initWithFrame:(NSRect)frameRect {
  // BookmarkButton's ViewID may be changed to VIEW_ID_OTHER_BOOKMARKS in
  // BookmarkBarController, so we can't just override -viewID method to return
  // it.
  if ((self = [super initWithFrame:frameRect]))
    view_id_util::SetID(self, VIEW_ID_BOOKMARK_BAR_ELEMENT);
  return self;
}

- (void)dealloc {
  if ([[self cell] respondsToSelector:@selector(safelyStopPulsing)])
    [[self cell] safelyStopPulsing];
  view_id_util::UnsetID(self);
  [super dealloc];
}

- (const BookmarkNode*)bookmarkNode {
  return [[self cell] bookmarkNode];
}

- (BOOL)isFolder {
  const BookmarkNode* node = [self bookmarkNode];
  return (node && node->is_folder());
}

- (BOOL)isEmpty {
  return [self bookmarkNode] ? NO : YES;
}

- (void)setIsContinuousPulsing:(BOOL)flag {
  [[self cell] setIsContinuousPulsing:flag];
}

- (BOOL)isContinuousPulsing {
  return [[self cell] isContinuousPulsing];
}

- (NSPoint)screenLocationForRemoveAnimation {
  NSPoint point;

  if (dragPending_) {
    // Use the position of the mouse in the drag image as the location.
    point = dragEndScreenLocation_;
    point.x += dragMouseOffset_.x;
    if ([self isFlipped]) {
      point.y += [self bounds].size.height - dragMouseOffset_.y;
    } else {
      point.y += dragMouseOffset_.y;
    }
  } else {
    // Use the middle of this button as the location.
    NSRect bounds = [self bounds];
    point = NSMakePoint(NSMidX(bounds), NSMidY(bounds));
    point = [self convertPoint:point toView:nil];
    point = [[self window] convertBaseToScreen:point];
  }

  return point;
}

// By default, NSButton ignores middle-clicks.
// But we want them.
- (void)otherMouseUp:(NSEvent*)event {
  [self performClick:self];
}

// Overridden from DraggableButton.
- (void)beginDrag:(NSEvent*)event {
  // Don't allow a drag of the empty node.
  // The empty node is a placeholder for "(empty)", to be revisited.
  if ([self isEmpty])
    return;

  if ([self delegate]) {
    // Ask our delegate to fill the pasteboard for us.
    NSPasteboard* pboard = [NSPasteboard pasteboardWithName:NSDragPboard];
    [[self delegate] fillPasteboard:pboard forDragOfButton:self];

    // At the moment, moving bookmarks causes their buttons (like me!)
    // to be destroyed and rebuilt.  Make sure we don't go away while on
    // the stack.
    [self retain];

    // Lock bar visibility, forcing the overlay to stay visible if we are in
    // fullscreen mode.
    if ([[self delegate] dragShouldLockBarVisibility]) {
      DCHECK(!visibilityDelegate_);
      NSWindow* window = [[self delegate] browserWindow];
      visibilityDelegate_ =
          [BrowserWindowController browserWindowControllerForWindow:window];
      [visibilityDelegate_ lockBarVisibilityForOwner:self
                                       withAnimation:NO
                                               delay:NO];
    }
    const BookmarkNode* node = [self bookmarkNode];
    const BookmarkNode* parent = node ? node->GetParent() : NULL;
    BOOL isWithinFolder = parent && parent->type() == BookmarkNode::FOLDER;
    UserMetrics::RecordAction(UserMetricsAction(
        isWithinFolder ? "BookmarkBarFolder_DragStart" :
            "BookmarkBar_DragStart"));

    dragMouseOffset_ = [self convertPointFromBase:[event locationInWindow]];
    dragPending_ = YES;

    CGFloat yAt = [self bounds].size.height;
    NSSize dragOffset = NSMakeSize(0.0, 0.0);
    [self dragImage:[self dragImage] at:NSMakePoint(0, yAt) offset:dragOffset
              event:event pasteboard:pboard source:self slideBack:YES];

    // And we're done.
    dragPending_ = NO;
    [self autorelease];
  } else {
    // Avoid blowing up, but we really shouldn't get here.
    NOTREACHED();
  }
}

// Overridden to release bar visibility.
- (void)endDrag {
  // visibilityDelegate_ can be nil if we're detached, and that's fine.
  [visibilityDelegate_ releaseBarVisibilityForOwner:self
                                      withAnimation:YES
                                              delay:YES];
  visibilityDelegate_ = nil;
  [super endDrag];
}

- (NSDragOperation)draggingSourceOperationMaskForLocal:(BOOL)isLocal {
  NSDragOperation operation = NSDragOperationCopy;
  if (isLocal) {
    operation |= NSDragOperationMove;
  }
  if ([delegate_ canDragBookmarkButtonToTrash:self]) {
    operation |= NSDragOperationDelete;
  }
  return operation;
}

- (void)draggedImage:(NSImage *)anImage
             endedAt:(NSPoint)aPoint
           operation:(NSDragOperation)operation {
  if (operation & NSDragOperationDelete) {
    dragEndScreenLocation_ = aPoint;
    [delegate_ didDragBookmarkToTrash:self];
  }
}

// mouseEntered: and mouseExited: are called from our
// BookmarkButtonCell.  We redirect this information to our delegate.
// The controller can then perform menu-like actions (e.g. "hover over
// to open menu").
- (void)mouseEntered:(NSEvent*)event {
  [delegate_ mouseEnteredButton:self event:event];
}

// See comments above mouseEntered:.
- (void)mouseExited:(NSEvent*)event {
  [delegate_ mouseExitedButton:self event:event];
}

@end

@implementation BookmarkButton(Private)

- (NSImage*)dragImage {
  NSRect bounds = [self bounds];

  // Grab the image from the screen and put it in an |NSImage|. We can't use
  // this directly since we need to clip it and set its opacity. This won't work
  // if the source view is clipped. Fortunately, we don't display clipped
  // bookmark buttons.
  [self lockFocus];
  scoped_nsobject<NSBitmapImageRep>
      bitmap([[NSBitmapImageRep alloc] initWithFocusedViewRect:bounds]);
  [self unlockFocus];
  scoped_nsobject<NSImage> image([[NSImage alloc] initWithSize:[bitmap size]]);
  [image addRepresentation:bitmap];

  // Make an autoreleased |NSImage|, which will be returned, and draw into it.
  // By default, the |NSImage| will be completely transparent.
  NSImage* dragImage =
      [[[NSImage alloc] initWithSize:[bitmap size]] autorelease];
  [dragImage lockFocus];

  // Draw the image with the appropriate opacity, clipping it tightly.
  GradientButtonCell* cell = static_cast<GradientButtonCell*>([self cell]);
  DCHECK([cell isKindOfClass:[GradientButtonCell class]]);
  [[cell clipPathForFrame:bounds inView:self] setClip];
  [image drawAtPoint:NSMakePoint(0, 0)
            fromRect:NSMakeRect(0, 0, NSWidth(bounds), NSHeight(bounds))
           operation:NSCompositeSourceOver
            fraction:kDragImageOpacity];

  [dragImage unlockFocus];
  return dragImage;
}

@end  // @implementation BookmarkButton(Private)
