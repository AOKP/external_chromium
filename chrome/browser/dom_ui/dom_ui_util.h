// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DOM_UI_DOM_UI_UTIL_H_
#define CHROME_BROWSER_DOM_UI_DOM_UI_UTIL_H_
#pragma once

#include <string>

class ListValue;
class SkBitmap;

namespace dom_ui_util {

// Convenience routine to get the response string from an argument
// list.  Typically used when supporting a DOMUI and getting calls
// from the hosted code.  Content must be a ListValue with at least
// one entry in it, and that first entry must be a string, which is
// returned.  The parameter is a Value for convenience.  Returns an
// empty string on error or if the parameter is not a ListValue.
std::string GetJsonResponseFromFirstArgumentInList(const ListValue* args);

// Convenience routine to get one of the response strings from an
// argument list.  content must be a ListValue, with at least
// (list_index+1) entries in it.  list_index is the 0-based index of
// the entry to pull from that list, and that entry must be a string,
// which is returned.  The parameter is a Value for convenience.
// Returns an empty string on error or if the parameter is not a
// ListValue.
std::string GetJsonResponseFromArgumentList(const ListValue* args,
                                            size_t list_index);


// Convenience routine to convert SkBitmap object to data url
// so that it can be used in DOMUI.
std::string GetImageDataUrl(const SkBitmap& bitmap);

// Convenience routine to get data url that corresponds to given
// resource_id as an image. This function does not check if the
// resource for the |resource_id| is an image, therefore it is the
// caller's responsibility to make sure the resource is indeed an
// image. Returns empty string if a resource does not exist for given
// |resource_id|.
std::string GetImageDataUrlFromResource(int resource_id);

}  // end of namespace

#endif  // CHROME_BROWSER_DOM_UI_DOM_UI_UTIL_H_
