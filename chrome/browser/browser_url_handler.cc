// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/browser_url_handler.h"

#include "base/string_util.h"
#include "chrome/browser/browser_about_handler.h"
#include "chrome/browser/dom_ui/dom_ui_factory.h"
#include "chrome/browser/extensions/extension_dom_ui.h"
#include "chrome/browser/profile.h"
#include "chrome/common/url_constants.h"
#include "googleurl/src/gurl.h"

// Handles rewriting view-source URLs for what we'll actually load.
static bool HandleViewSource(GURL* url, Profile* profile) {
  if (url->SchemeIs(chrome::kViewSourceScheme)) {
    // Load the inner URL instead.
    *url = GURL(url->path());

    // Bug 26129: limit view-source to view the content and not any
    // other kind of 'active' url scheme like 'javascript' or 'data'.
    static const char* const allowed_sub_schemes[] = {
      chrome::kHttpScheme, chrome::kHttpsScheme, chrome::kFtpScheme,
      chrome::kChromeDevToolsScheme, chrome::kChromeUIScheme,
      chrome::kFileScheme
    };

    bool is_sub_scheme_allowed = false;
    for (size_t i = 0; i < arraysize(allowed_sub_schemes); i++) {
      if (url->SchemeIs(allowed_sub_schemes[i])) {
        is_sub_scheme_allowed = true;
        break;
      }
    }

    if (!is_sub_scheme_allowed) {
      *url = GURL(chrome::kAboutBlankURL);
      return false;
    }

    return true;
  }
  return false;
}

// Turns a non view-source URL into the corresponding view-source URL.
static bool ReverseViewSource(GURL* url, Profile* profile) {
  // No action necessary if the URL is already view-source:
  if (url->SchemeIs(chrome::kViewSourceScheme))
    return false;

  url_canon::Replacements<char> repl;
  repl.SetScheme(chrome::kViewSourceScheme,
      url_parse::Component(0, strlen(chrome::kViewSourceScheme)));
  repl.SetPath(url->spec().c_str(),
      url_parse::Component(0, url->spec().size()));
  *url = url->ReplaceComponents(repl);
  return true;
}

// Handles rewriting DOM UI URLs.
static bool HandleDOMUI(GURL* url, Profile* profile) {
  if (!DOMUIFactory::UseDOMUIForURL(profile, *url))
    return false;

  // Special case the new tab page. In older versions of Chrome, the new tab
  // page was hosted at chrome-internal:<blah>. This might be in people's saved
  // sessions or bookmarks, so we say any URL with that scheme triggers the new
  // tab page.
  if (url->SchemeIs(chrome::kChromeInternalScheme)) {
    // Rewrite it with the proper new tab URL.
    *url = GURL(chrome::kChromeUINewTabURL);
  }

  return true;
}

std::vector<BrowserURLHandler::HandlerPair> BrowserURLHandler::url_handlers_;

// static
void BrowserURLHandler::InitURLHandlers() {
  if (!url_handlers_.empty())
    return;

  // Add the default URL handlers.
  url_handlers_.push_back(
      HandlerPair(&ExtensionDOMUI::HandleChromeURLOverride, NULL));
  // about:
  url_handlers_.push_back(HandlerPair(&WillHandleBrowserAboutURL, NULL));
  // chrome: & friends.
  url_handlers_.push_back(HandlerPair(&HandleDOMUI, NULL));
  // view-source:
  url_handlers_.push_back(HandlerPair(&HandleViewSource, &ReverseViewSource));
}

// static
void BrowserURLHandler::RewriteURLIfNecessary(GURL* url, Profile* profile,
                                              bool* reverse_on_redirect) {
  if (url_handlers_.empty())
    InitURLHandlers();
  for (size_t i = 0; i < url_handlers_.size(); ++i) {
    if ((*url_handlers_[i].first)(url, profile)) {
      *reverse_on_redirect = (url_handlers_[i].second != NULL);
      return;
    }
  }
}

// static
bool BrowserURLHandler::ReverseURLRewrite(
    GURL* url, const GURL& original, Profile* profile) {
  for (size_t i = 0; i < url_handlers_.size(); ++i) {
    GURL test_url(original);
    if ((*url_handlers_[i].first)(&test_url, profile)) {
      if (url_handlers_[i].second)
        return (*url_handlers_[i].second)(url, profile);
      else
        return false;
    }
  }
  return false;
}
