// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/defaults.h"

namespace browser_defaults {

#if defined(OS_CHROMEOS)

// Make the regular omnibox text two points larger than the nine-point font
// used in the tab strip (11pt / 72pt/in * 96px/in = 14.667px).
const double kAutocompleteEditFontPixelSize = 14.7;
const double kAutocompleteEditFontPixelSizeInPopup = 10.0;

// This is only used by AutocompletePopupViewGtk which is unused
// unless TOOLKIT_VIEWS is undefined:
const int kAutocompletePopupFontSize = 7;

const SessionStartupPref::Type kDefaultSessionStartupType =
    SessionStartupPref::LAST;
const int kMiniTabWidth = 64;
const bool kCanToggleSystemTitleBar = false;
const bool kRestorePopups = true;
const bool kShowImportOnBookmarkBar = false;
const bool kShowExitMenuItem = true;
const bool kShowAboutMenuItem = true;
const bool kOSSupportsOtherBrowsers = false;
const bool kDownloadPageHasShowInFolder = true;
const bool kSizeTabButtonToTopOfTabStrip = true;
const bool kBootstrapSyncAuthentication = true;
const bool kShowOtherBrowsersInAboutMemory = false;
const bool kAlwaysOpenIncognitoWindow = true;

#elif defined(TOOLKIT_USES_GTK)

// 13.4px = 10pt @ 96dpi.
const double kAutocompleteEditFontPixelSize = 13.4;

// On Windows, popup windows' autocomplete box have a font 5/6 the size of a
// regular window, which we duplicate here for GTK.
const double kAutocompleteEditFontPixelSizeInPopup =
    kAutocompleteEditFontPixelSize * 5.0 / 6.0;

const int kAutocompletePopupFontSize = 10;

#if defined(TOOLKIT_VIEWS)
const bool kCanToggleSystemTitleBar = false;
#else
const bool kCanToggleSystemTitleBar = true;
#endif

#endif

#if !defined(OS_CHROMEOS)

const SessionStartupPref::Type kDefaultSessionStartupType =
    SessionStartupPref::DEFAULT;
const int kMiniTabWidth = 56;
const bool kRestorePopups = false;
const bool kShowImportOnBookmarkBar = true;
const bool kDownloadPageHasShowInFolder = true;
#if defined(OS_MACOSX)
const bool kShowExitMenuItem = false;
const bool kShowAboutMenuItem = false;
#else
const bool kShowExitMenuItem = true;
const bool kShowAboutMenuItem = true;
#endif
const bool kOSSupportsOtherBrowsers = true;
const bool kSizeTabButtonToTopOfTabStrip = false;
const bool kBootstrapSyncAuthentication = false;
const bool kShowOtherBrowsersInAboutMemory = true;
const bool kAlwaysOpenIncognitoWindow = false;
#endif

#if defined(OS_MACOSX)
const bool kBrowserAliveWithNoWindows = true;
#else
const bool kBrowserAliveWithNoWindows = false;
#endif

const bool kPhantomTabsEnabled = false;

bool bookmarks_enabled = true;

}  // namespace browser_defaults
