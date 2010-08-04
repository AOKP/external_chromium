// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <Cocoa/Cocoa.h>
#import "chrome/browser/cocoa/chrome_event_processing_window.h"

// A FullscreenWindow is a borderless window suitable for going
// fullscreen.  The returned window is NOT release when closed and is
// not initially visible.
// FullscreenWindow derives from ChromeEventProcessingWindow to inherit
// special event handling (e.g. handleExtraKeyboardShortcut).
@interface FullscreenWindow : ChromeEventProcessingWindow

// Initialize a FullscreenWindow for the given screen.
// Designated initializer.
- (id)initForScreen:(NSScreen*)screen;

@end
