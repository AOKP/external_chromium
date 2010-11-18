// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/search_engines/search_terms_data.h"

#include "base/logging.h"
#include "base/thread_restrictions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_thread.h"
#include "chrome/browser/google/google_url_tracker.h"
#include "googleurl/src/gurl.h"

#if defined(OS_WIN) && defined(GOOGLE_CHROME_BUILD)
#include "chrome/browser/rlz/rlz.h"
#include "chrome/installer/util/google_update_settings.h"
#endif

SearchTermsData::SearchTermsData() {
}

SearchTermsData::~SearchTermsData() {
}

std::string SearchTermsData::GoogleBaseSuggestURLValue() const {
  // The suggest base URL we want at the end is something like
  // "http://clients1.google.TLD/complete/".  The key bit we want from the
  // original Google base URL is the TLD.

  // Start with the Google base URL.
  const GURL base_url(GoogleBaseURLValue());
  DCHECK(base_url.is_valid());

  // Change "www." to "clients1." in the hostname.  If no "www." was found, just
  // prepend "clients1.".
  const std::string base_host(base_url.host());
  GURL::Replacements repl;
  const std::string suggest_host("clients1." +
      (base_host.compare(0, 4, "www.") ? base_host : base_host.substr(4)));
  repl.SetHostStr(suggest_host);

  // Replace any existing path with "/complete/".
  static const std::string suggest_path("/complete/");
  repl.SetPathStr(suggest_path);

  // Clear the query and ref.
  repl.ClearQuery();
  repl.ClearRef();
  return base_url.ReplaceComponents(repl).spec();
}

// static
std::string* UIThreadSearchTermsData::google_base_url_ = NULL;

UIThreadSearchTermsData::UIThreadSearchTermsData() {
  // GoogleURLTracker::GoogleURL() DCHECKs this also, but adding it here helps
  // us catch bad behavior at a more common place in this code.
  DCHECK(!BrowserThread::IsWellKnownThread(BrowserThread::UI) ||
         BrowserThread::CurrentlyOn(BrowserThread::UI));
}

std::string UIThreadSearchTermsData::GoogleBaseURLValue() const {
  DCHECK(!BrowserThread::IsWellKnownThread(BrowserThread::UI) ||
         BrowserThread::CurrentlyOn(BrowserThread::UI));
  return google_base_url_ ?
    (*google_base_url_) : GoogleURLTracker::GoogleURL().spec();
}

std::string UIThreadSearchTermsData::GetApplicationLocale() const {
  DCHECK(!BrowserThread::IsWellKnownThread(BrowserThread::UI) ||
         BrowserThread::CurrentlyOn(BrowserThread::UI));
  return g_browser_process->GetApplicationLocale();
}

#if defined(OS_WIN) && defined(GOOGLE_CHROME_BUILD)
std::wstring UIThreadSearchTermsData::GetRlzParameterValue() const {
  DCHECK(!BrowserThread::IsWellKnownThread(BrowserThread::UI) ||
         BrowserThread::CurrentlyOn(BrowserThread::UI));
  std::wstring rlz_string;
  // For organic brandcodes do not use rlz at all. Empty brandcode usually
  // means a chromium install. This is ok.
  std::wstring brand;
  // See http://crbug.com/62337.
  base::ThreadRestrictions::ScopedAllowIO allow_io;
  if (GoogleUpdateSettings::GetBrand(&brand) && !brand.empty() &&
      !GoogleUpdateSettings::IsOrganic(brand))
    RLZTracker::GetAccessPointRlz(rlz_lib::CHROME_OMNIBOX, &rlz_string);
  return rlz_string;
}
#endif

// static
void UIThreadSearchTermsData::SetGoogleBaseURL(std::string* google_base_url) {
  delete google_base_url_;
  google_base_url_ = google_base_url;
}
