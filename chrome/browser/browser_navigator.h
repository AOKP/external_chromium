// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_BROWSER_NAVIGATOR_H_
#define CHROME_BROWSER_BROWSER_NAVIGATOR_H_
#pragma once

#include <string>

#include "chrome/common/page_transition_types.h"
#include "gfx/rect.h"
#include "googleurl/src/gurl.h"
#include "webkit/glue/window_open_disposition.h"

class Browser;
class Profile;
class TabContents;

namespace browser {

// Parameters that tell Navigate() what to do.
//
// Some basic examples:
//
// Simple Navigate to URL in current tab:
// browser::NavigateParams params(browser, GURL("http://www.google.com/"),
//                                PageTransition::LINK);
// browser::Navigate(&params);
//
// Open bookmark in new background tab:
// browser::NavigateParams params(browser, url, PageTransition::AUTO_BOOKMARK);
// params.disposition = NEW_BACKGROUND_TAB;
// browser::Navigate(&params);
//
// Opens a popup TabContents:
// browser::NavigateParams params(browser, popup_contents);
// params.source_contents = source_contents;
// browser::Navigate(&params);
//
// See browser_navigator_browsertest.cc for more examples.
//
struct NavigateParams {
  NavigateParams(Browser* browser,
                 const GURL& a_url,
                 PageTransition::Type a_transition);
  NavigateParams(Browser* browser, TabContents* a_target_contents);
  ~NavigateParams();

  // The URL/referrer to be loaded. Can be empty if |contents| is specified
  // non-NULL.
  GURL url;
  GURL referrer;

  // [in]  A TabContents to be navigated or inserted into the target Browser's
  //       tabstrip. If NULL, |url| or the homepage will be used instead.
  //       Default is NULL.
  // [out] The TabContents in which the navigation occurred or that was
  //       inserted. Guaranteed non-NULL except for note below:
  // Note: If this field is set to NULL by the caller and Navigate() creates
  //       a new TabContents, this field will remain NULL and the TabContents
  //       deleted if the TabContents it created is not added to a TabStripModel
  //       before Navigate() returns.
  TabContents* target_contents;

  // [in]  The TabContents that initiated the Navigate() request if such context
  //       is necessary. Default is NULL, i.e. no context.
  // [out] If NULL, this value will be set to the selected TabContents in the
  //       originating browser prior to the operation performed by Navigate().
  TabContents* source_contents;

  // The disposition requested by the navigation source. Default is
  // CURRENT_TAB.
  WindowOpenDisposition disposition;

  // The transition type of the navigation. Default is PageTransition::LINK
  // when target_contents is specified in the constructor.
  PageTransition::Type transition;

  // The index the caller would like the tab to be positioned at in the
  // TabStrip. The actual index will be determined by the TabHandler in
  // accordance with |add_types|. Defaults to -1 (allows the TabHandler to
  // decide).
  int tabstrip_index;

  // A bitmask of values defined in TabStripModel::AddTabTypes. Helps
  // determine where to insert a new tab and whether or not it should be
  // selected, among other properties. Default is ADD_SELECTED.
  int tabstrip_add_types;

  // If non-empty, the new tab is an app tab.
  std::string extension_app_id;

  // If non-empty, specifies the desired initial position and size of the
  // window if |disposition| == NEW_POPUP.
  // TODO(beng): Figure out if this can be used to create Browser windows
  //             for other callsites that use set_override_bounds, or
  //             remove this comment.
  gfx::Rect window_bounds;

  // True if the target window should be made visible at the end of the call
  // to Navigate(). Default is false.
  bool show_window;

  // [in]  Specifies a Browser object where the navigation could occur or the
  //       tab could be added. Navigate() is not obliged to use this Browser if
  //       it is not compatible with the operation being performed. Cannot be
  //       NULL since Navigate() uses this Browser's Profile.
  // [out] Specifies the Browser object where the navigation occurred or the
  //       tab was added. Guaranteed non-NULL unless the disposition did not
  //       require a navigation, in which case this is set to NULL
  //       (SUPPRESS_OPEN, SAVE_TO_DISK, IGNORE_ACTION).
  // Note: If |show_window| is set to false and a new Browser is created by
  //       Navigate(), the caller is responsible for showing it so that its
  //       window can assume responsibility for the Browser's lifetime (Browser
  //       objects are deleted when the user closes a visible browser window).
  Browser* browser;

 private:
  NavigateParams();
};

// Navigates according to the configuration specified in |params|.
void Navigate(NavigateParams* params);

}  // namespace browser

#endif  // CHROME_BROWSER_BROWSER_NAVIGATOR_H_

