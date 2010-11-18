// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_COCOA_NOTIFICATIONS_BALLOON_CONTROLLER_H_
#define CHROME_BROWSER_COCOA_NOTIFICATIONS_BALLOON_CONTROLLER_H_
#pragma once

#import <Cocoa/Cocoa.h>

#include "base/cocoa_protocols_mac.h"
#include "base/scoped_nsobject.h"
#include "base/scoped_ptr.h"

class Balloon;
@class BalloonContentViewCocoa;
@class BalloonShelfViewCocoa;
class BalloonViewHost;
@class HoverImageButton;
@class MenuController;
class NotificationOptionsMenuModel;

// The Balloon controller creates the view elements to display a
// notification balloon, resize it if the HTML contents of that
// balloon change, and move it when the collection of balloons is
// modified.
@interface BalloonController : NSWindowController<NSWindowDelegate> {
 @private
  // The balloon which represents the contents of this view. Weak pointer
  // owned by the browser's NotificationUIManager.
  Balloon* balloon_;

  // The view that contains the contents of the notification
  IBOutlet BalloonContentViewCocoa* htmlContainer_;

  // The view that contains the controls of the notification
  IBOutlet BalloonShelfViewCocoa* shelf_;

  // The close button.
  IBOutlet NSButton* closeButton_;

  // Tracking region for the close button.
  int closeButtonTrackingTag_;

  // The origin label.
  IBOutlet NSTextField* originLabel_;

  // The options menu that appears when "options" is pressed.
  IBOutlet HoverImageButton* optionsButton_;
  scoped_ptr<NotificationOptionsMenuModel> menuModel_;
  scoped_nsobject<MenuController> menuController_;

  // The host for the renderer of the HTML contents.
  scoped_ptr<BalloonViewHost> htmlContents_;

  // The psn of the front application process.
  ProcessSerialNumber frontProcessNum_;
}

// Initialize with a balloon object containing the notification data.
- (id)initWithBalloon:(Balloon*)balloon;

// Callback function for the close button.
- (IBAction)closeButtonPressed:(id)sender;

// Callback function for the options button.
- (IBAction)optionsButtonPressed:(id)sender;

// Callback function for the "revoke" option in the menu.
- (IBAction)permissionRevoked:(id)sender;

// Closes the balloon.  Can be called by the bridge or by the close
// button handler.
- (void)closeBalloon:(bool)byUser;

// Update the contents of the balloon to match the notification.
- (void)updateContents;

// Repositions the view to match the position and size of the balloon.
// Called by the bridge when the size changes.
- (void)repositionToBalloon;

// The current size of the view, possibly subject to an animation completing.
- (int)desiredTotalWidth;
- (int)desiredTotalHeight;

// The BalloonHost
- (BalloonViewHost*)getHost;

// Handle the event if it is for the balloon.
- (BOOL)handleEvent:(NSEvent*)event;
@end

@interface BalloonController (UnitTesting)
- (void)initializeHost;
@end

#endif  // CHROME_BROWSER_COCOA_NOTIFICATIONS_BALLOON_CONTROLLER_H_
