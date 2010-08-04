// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/glue/webpreferences.h"

#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "third_party/WebKit/WebKit/chromium/public/WebRuntimeFeatures.h"
#include "third_party/WebKit/WebKit/chromium/public/WebKit.h"
#include "third_party/WebKit/WebKit/chromium/public/WebSettings.h"
#include "third_party/WebKit/WebKit/chromium/public/WebString.h"
#include "third_party/WebKit/WebKit/chromium/public/WebURL.h"
#include "third_party/WebKit/WebKit/chromium/public/WebView.h"
#include "webkit/glue/webkit_glue.h"

using WebKit::WebRuntimeFeatures;
using WebKit::WebSettings;
using WebKit::WebString;
using WebKit::WebURL;
using WebKit::WebView;

void WebPreferences::Apply(WebView* web_view) const {
  WebSettings* settings = web_view->settings();
  settings->setStandardFontFamily(WideToUTF16Hack(standard_font_family));
  settings->setFixedFontFamily(WideToUTF16Hack(fixed_font_family));
  settings->setSerifFontFamily(WideToUTF16Hack(serif_font_family));
  settings->setSansSerifFontFamily(WideToUTF16Hack(sans_serif_font_family));
  settings->setCursiveFontFamily(WideToUTF16Hack(cursive_font_family));
  settings->setFantasyFontFamily(WideToUTF16Hack(fantasy_font_family));
  settings->setDefaultFontSize(default_font_size);
  settings->setDefaultFixedFontSize(default_fixed_font_size);
  settings->setMinimumFontSize(minimum_font_size);
  settings->setMinimumLogicalFontSize(minimum_logical_font_size);
  settings->setDefaultTextEncodingName(ASCIIToUTF16(default_encoding));
  settings->setJavaScriptEnabled(javascript_enabled);
  settings->setWebSecurityEnabled(web_security_enabled);
  settings->setJavaScriptCanOpenWindowsAutomatically(
      javascript_can_open_windows_automatically);
  settings->setLoadsImagesAutomatically(loads_images_automatically);
  settings->setPluginsEnabled(plugins_enabled);
  settings->setDOMPasteAllowed(dom_paste_enabled);
  settings->setDeveloperExtrasEnabled(developer_extras_enabled);
  settings->setNeedsSiteSpecificQuirks(site_specific_quirks_enabled);
  settings->setShrinksStandaloneImagesToFit(shrinks_standalone_images_to_fit);
  settings->setUsesEncodingDetector(uses_universal_detector);
  settings->setTextAreasAreResizable(text_areas_are_resizable);
  settings->setAllowScriptsToCloseWindows(allow_scripts_to_close_windows);
  if (user_style_sheet_enabled)
    settings->setUserStyleSheetLocation(user_style_sheet_location);
  else
    settings->setUserStyleSheetLocation(WebURL());
  settings->setAuthorAndUserStylesEnabled(author_and_user_styles_enabled);
  settings->setUsesPageCache(uses_page_cache);
  settings->setDownloadableBinaryFontsEnabled(remote_fonts_enabled);
  settings->setJavaScriptCanAccessClipboard(javascript_can_access_clipboard);
  settings->setXSSAuditorEnabled(xss_auditor_enabled);
  settings->setLocalStorageEnabled(local_storage_enabled);
  WebRuntimeFeatures::enableDatabase(
      WebRuntimeFeatures::isDatabaseEnabled() || databases_enabled);
  settings->setOfflineWebApplicationCacheEnabled(application_cache_enabled);
  settings->setHTML5ParserEnabled(enable_html5_parser);

  // This setting affects the behavior of links in an editable region:
  // clicking the link should select it rather than navigate to it.
  // Safari uses the same default. It is unlikley an embedder would want to
  // change this, since it would break existing rich text editors.
  settings->setEditableLinkBehaviorNeverLive();

  settings->setFontRenderingModeNormal();
  settings->setJavaEnabled(java_enabled);

  // Turn this on to cause WebCore to paint the resize corner for us.
  settings->setShouldPaintCustomScrollbars(true);

  // By default, allow_universal_access_from_file_urls is set to false and thus
  // we mitigate attacks from local HTML files by not granting file:// URLs
  // universal access. Only test shell will enable this.
  settings->setAllowUniversalAccessFromFileURLs(
      allow_universal_access_from_file_urls);
  settings->setAllowFileAccessFromFileURLs(allow_file_access_from_file_urls);

  // We prevent WebKit from checking if it needs to add a "text direction"
  // submenu to a context menu. it is not only because we don't need the result
  // but also because it cause a possible crash in Editor::hasBidiSelection().
  settings->setTextDirectionSubmenuInclusionBehaviorNeverIncluded();

  // Enable experimental WebGL support if requested on command line
  // and support is compiled in.
  settings->setExperimentalWebGLEnabled(experimental_webgl_enabled);

  // Display colored borders around composited render layers if requested
  // on command line.
  settings->setShowDebugBorders(show_composited_layer_borders);

  // Enable gpu-accelerated compositing if requested on the command line.
  settings->setAcceleratedCompositingEnabled(accelerated_compositing_enabled);

  // Enable memory info reporting to page if requested on the command line.
  settings->setMemoryInfoEnabled(memory_info_enabled);

  for (WebInspectorPreferences::const_iterator it = inspector_settings.begin();
       it != inspector_settings.end(); ++it)
    web_view->setInspectorSetting(WebString::fromUTF8(it->first),
                                  WebString::fromUTF8(it->second));

  // Tabs to link is not part of the settings. WebCore calls
  // ChromeClient::tabsToLinks which is part of the glue code.
  web_view->setTabsToLinks(tabs_to_links);
}
