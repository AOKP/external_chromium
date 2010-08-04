// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Constants used for the Tabs API and the Windows API.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_TABS_MODULE_CONSTANTS_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_TABS_MODULE_CONSTANTS_H_

namespace extension_tabs_module_constants {

// Keys used in serializing tab data & events.
extern const wchar_t kAllFramesKey[];
extern const wchar_t kCodeKey[];
extern const wchar_t kFavIconUrlKey[];
extern const wchar_t kFileKey[];
extern const wchar_t kFocusedKey[];
extern const wchar_t kFormatKey[];
extern const wchar_t kFromIndexKey[];
extern const wchar_t kHeightKey[];
extern const wchar_t kIdKey[];
extern const wchar_t kIndexKey[];
extern const wchar_t kLeftKey[];
extern const wchar_t kNewPositionKey[];
extern const wchar_t kNewWindowIdKey[];
extern const wchar_t kOldPositionKey[];
extern const wchar_t kOldWindowIdKey[];
extern const wchar_t kPopulateKey[];
extern const wchar_t kQualityKey[];
extern const wchar_t kSelectedKey[];
extern const wchar_t kStatusKey[];
extern const wchar_t kTabIdKey[];
extern const wchar_t kTabsKey[];
extern const wchar_t kTabUrlKey[];
extern const wchar_t kTitleKey[];
extern const wchar_t kToIndexKey[];
extern const wchar_t kTopKey[];
extern const wchar_t kUrlKey[];
extern const wchar_t kWidthKey[];
extern const wchar_t kWindowIdKey[];
extern const wchar_t kIncognitoKey[];
extern const wchar_t kWindowTypeKey[];

// Value consts.
extern const char kCanOnlyMoveTabsWithinNormalWindowsError[];
extern const char kFormatValueJpeg[];
extern const char kFormatValuePng[];
extern const char kMimeTypeJpeg[];
extern const char kMimeTypePng[];
extern const char kStatusValueComplete[];
extern const char kStatusValueLoading[];
extern const char kWindowTypeValueNormal[];
extern const char kWindowTypeValuePopup[];
extern const char kWindowTypeValueApp[];

// Error messages.
extern const char kNoCurrentWindowError[];
extern const char kNoLastFocusedWindowError[];
extern const char kWindowNotFoundError[];
extern const char kTabNotFoundError[];
extern const char kNoSelectedTabError[];
extern const char kInvalidUrlError[];
extern const char kInternalVisibleTabCaptureError[];
extern const char kNotImplementedError[];
extern const char kSupportedInWindowsOnlyError[];

extern const char kNoCodeOrFileToExecuteError[];
extern const char kMoreThanOneValuesError[];
extern const char kLoadFileError[];
extern const char kCannotUpdatePinnedTab[];
extern const char kCannotRemovePhantomTab[];
extern const char kCannotDetermineLanguageOfUnloadedTab[];

};  // namespace extension_tabs_module_constants

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_TABS_MODULE_CONSTANTS_H_
