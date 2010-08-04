// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_COCOA_BOOKMARK_ALL_TABS_CONTROLLER_H_
#define CHROME_BROWSER_COCOA_BOOKMARK_ALL_TABS_CONTROLLER_H_

#include <utility>
#include <vector>

#import "chrome/browser/cocoa/bookmark_editor_base_controller.h"

// A list of pairs containing the name and URL associated with each
// currently active tab in the active browser window.
typedef std::pair<std::wstring, GURL> ActiveTabNameURLPair;
typedef std::vector<ActiveTabNameURLPair> ActiveTabsNameURLPairVector;

// A controller for the Bookmark All Tabs sheet which is presented upon
// selecting the Bookmark All Tabs... menu item shown by the contextual
// menu in the bookmarks bar.
@interface BookmarkAllTabsController : BookmarkEditorBaseController {
 @private
  ActiveTabsNameURLPairVector activeTabPairsVector_;
}

- (id)initWithParentWindow:(NSWindow*)parentWindow
                   profile:(Profile*)profile
                    parent:(const BookmarkNode*)parent
             configuration:(BookmarkEditor::Configuration)configuration;

@end

@interface BookmarkAllTabsController(TestingAPI)

// Initializes the list of all tab names and URLs.  Overridden by unit test
// to provide canned test data.
- (void)UpdateActiveTabPairs;

// Provides testing access to tab pairs list.
- (ActiveTabsNameURLPairVector*)activeTabPairsVector;

@end

#endif  // CHROME_BROWSER_COCOA_BOOKMARK_ALL_TABS_CONTROLLER_H_
