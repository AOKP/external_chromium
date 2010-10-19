// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/dom_ui/dom_ui_theme_source.h"

#include "app/resource_bundle.h"
#include "app/theme_provider.h"
#include "base/message_loop.h"
#include "base/ref_counted_memory.h"
#include "chrome/browser/chrome_thread.h"
#include "chrome/browser/dom_ui/ntp_resource_cache.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/resources_util.h"
#include "chrome/browser/themes/browser_theme_provider.h"
#include "chrome/common/url_constants.h"
#include "googleurl/src/gurl.h"

// use a resource map rather than hard-coded strings.
static const char* kNewTabCSSPath = "css/newtab.css";
static const char* kNewIncognitoTabCSSPath = "css/newincognitotab.css";

static std::string StripQueryParams(const std::string& path) {
  GURL path_url = GURL(std::string(chrome::kChromeUIScheme) + "://" +
                       std::string(chrome::kChromeUIThemePath) + "/" + path);
  return path_url.path().substr(1);  // path() always includes a leading '/'.
}

////////////////////////////////////////////////////////////////////////////////
// DOMUIThemeSource, public:

DOMUIThemeSource::DOMUIThemeSource(Profile* profile)
    : DataSource(chrome::kChromeUIThemePath, MessageLoop::current()),
      profile_(profile->GetOriginalProfile()) {
  css_bytes_ = profile_->GetNTPResourceCache()->GetNewTabCSS(
      profile->IsOffTheRecord());
}

DOMUIThemeSource::~DOMUIThemeSource() {
}

void DOMUIThemeSource::StartDataRequest(const std::string& path,
                                        bool is_off_the_record,
                                        int request_id) {
  // Our path may include cachebuster arguments, so trim them off.
  std::string uncached_path = StripQueryParams(path);

  if (uncached_path == kNewTabCSSPath ||
      uncached_path == kNewIncognitoTabCSSPath) {
    DCHECK(ChromeThread::CurrentlyOn(ChromeThread::IO));
    DCHECK((uncached_path == kNewTabCSSPath && !is_off_the_record) ||
           (uncached_path == kNewIncognitoTabCSSPath && is_off_the_record));

    SendResponse(request_id, css_bytes_);
    return;
  } else {
    int resource_id = ResourcesUtil::GetThemeResourceId(uncached_path);
    if (resource_id != -1) {
      SendThemeBitmap(request_id, resource_id);
      return;
    }
  }
  // We don't have any data to send back.
  SendResponse(request_id, NULL);
}

std::string DOMUIThemeSource::GetMimeType(const std::string& path) const {
  std::string uncached_path = StripQueryParams(path);

  if (uncached_path == kNewTabCSSPath ||
      uncached_path == kNewIncognitoTabCSSPath) {
    return "text/css";
  }

  return "image/png";
}

MessageLoop* DOMUIThemeSource::MessageLoopForRequestPath(
    const std::string& path) const {
  std::string uncached_path = StripQueryParams(path);

  if (uncached_path == kNewTabCSSPath ||
      uncached_path == kNewIncognitoTabCSSPath) {
    // We generated and cached this when we initialized the object.  We don't
    // have to go back to the UI thread to send the data.
    return NULL;
  }

  // If it's not a themeable image, we don't need to go to the UI thread.
  int resource_id = ResourcesUtil::GetThemeResourceId(uncached_path);
  if (!BrowserThemeProvider::IsThemeableImage(resource_id))
    return NULL;

  return DataSource::MessageLoopForRequestPath(path);
}

////////////////////////////////////////////////////////////////////////////////
// DOMUIThemeSource, private:

void DOMUIThemeSource::SendThemeBitmap(int request_id, int resource_id) {
  if (BrowserThemeProvider::IsThemeableImage(resource_id)) {
    DCHECK(ChromeThread::CurrentlyOn(ChromeThread::UI));
    ThemeProvider* tp = profile_->GetThemeProvider();
    DCHECK(tp);

    scoped_refptr<RefCountedMemory> image_data(tp->GetRawData(resource_id));
    SendResponse(request_id, image_data);
  } else {
    DCHECK(ChromeThread::CurrentlyOn(ChromeThread::IO));
    const ResourceBundle& rb = ResourceBundle::GetSharedInstance();
    SendResponse(request_id, rb.LoadDataResourceBytes(resource_id));
  }
}
