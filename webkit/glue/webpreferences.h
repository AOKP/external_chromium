// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// A struct for managing webkit's settings.
//
// Adding new values to this class probably involves updating
// WebKit::WebSettings, common/render_messages.h, and
// browser/profile.cc.

#ifndef WEBKIT_GLUE_WEBPREFERENCES_H__
#define WEBKIT_GLUE_WEBPREFERENCES_H__

#include <string>
#include <vector>
#include "googleurl/src/gurl.h"

namespace WebKit {
class WebView;
}

struct WebPreferences {
  std::wstring standard_font_family;
  std::wstring fixed_font_family;
  std::wstring serif_font_family;
  std::wstring sans_serif_font_family;
  std::wstring cursive_font_family;
  std::wstring fantasy_font_family;
  int default_font_size;
  int default_fixed_font_size;
  int minimum_font_size;
  int minimum_logical_font_size;
  std::string default_encoding;
  bool javascript_enabled;
  bool web_security_enabled;
  bool javascript_can_open_windows_automatically;
  bool loads_images_automatically;
  bool plugins_enabled;
  bool dom_paste_enabled;
  bool developer_extras_enabled;
  typedef std::vector<std::pair<std::string, std::string> >
      WebInspectorPreferences;
  WebInspectorPreferences inspector_settings;
  bool site_specific_quirks_enabled;
  bool shrinks_standalone_images_to_fit;
  bool uses_universal_detector;
  bool text_areas_are_resizable;
  bool java_enabled;
  bool allow_scripts_to_close_windows;
  bool uses_page_cache;
  bool remote_fonts_enabled;
  bool javascript_can_access_clipboard;
  bool xss_auditor_enabled;
  bool local_storage_enabled;
  bool databases_enabled;
  bool application_cache_enabled;
  bool tabs_to_links;
  bool caret_browsing_enabled;
  bool hyperlink_auditing_enabled;

  bool user_style_sheet_enabled;
  GURL user_style_sheet_location;
  bool author_and_user_styles_enabled;
  bool allow_universal_access_from_file_urls;
  bool allow_file_access_from_file_urls;
  bool experimental_webgl_enabled;
  bool show_composited_layer_borders;
  bool accelerated_compositing_enabled;
  bool accelerated_2d_canvas_enabled;
  bool memory_info_enabled;

  // We try to keep the default values the same as the default values in
  // chrome, except for the cases where it would require lots of extra work for
  // the embedder to use the same default value.
  WebPreferences();
  ~WebPreferences();

  void Apply(WebKit::WebView* web_view) const;
};

#endif  // WEBKIT_GLUE_WEBPREFERENCES_H__
