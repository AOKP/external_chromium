// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/cocoa/chrome_browser_window.h"

#include "base/logging.h"
#import "chrome/browser/cocoa/themed_window.h"
#include "chrome/browser/themes/browser_theme_provider.h"

@implementation ChromeBrowserWindow

- (void)underlaySurfaceAdded {
  DCHECK_GE(underlaySurfaceCount_, 0);
  ++underlaySurfaceCount_;

  // We're having the OpenGL surface render under the window, so the window
  // needs to be not opaque.
  if (underlaySurfaceCount_ == 1)
    [self setOpaque:NO];
}

- (void)underlaySurfaceRemoved {
  --underlaySurfaceCount_;
  DCHECK_GE(underlaySurfaceCount_, 0);

  if (underlaySurfaceCount_ == 0)
    [self setOpaque:YES];
}

- (ThemeProvider*)themeProvider {
  id delegate = [self delegate];
  if (![delegate respondsToSelector:@selector(themeProvider)])
    return NULL;
  return [delegate themeProvider];
}

- (ThemedWindowStyle)themedWindowStyle {
  id delegate = [self delegate];
  if (![delegate respondsToSelector:@selector(themedWindowStyle)])
    return THEMED_NORMAL;
  return [delegate themedWindowStyle];
}

- (NSPoint)themePatternPhase {
  id delegate = [self delegate];
  if (![delegate respondsToSelector:@selector(themePatternPhase)])
    return NSMakePoint(0, 0);
  return [delegate themePatternPhase];
}

@end
