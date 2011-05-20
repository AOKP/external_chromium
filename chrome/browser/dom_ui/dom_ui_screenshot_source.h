// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DOM_UI_DOM_UI_SCREENSHOT_SOURCE_H_
#define CHROME_BROWSER_DOM_UI_DOM_UI_SCREENSHOT_SOURCE_H_

#include <string>
#include <utility>
#include <vector>

#include "base/basictypes.h"
#include "base/scoped_ptr.h"
#include "chrome/browser/dom_ui/chrome_url_data_manager.h"
#include "chrome/browser/dom_ui/html_dialog_ui.h"

// ScreenshotSource is the data source that serves screenshots (saved
// or current) to the bug report html ui
class DOMUIScreenshotSource : public ChromeURLDataManager::DataSource {
 public:
  explicit DOMUIScreenshotSource(
      std::vector<unsigned char>* current_screenshot);

  // Called when the network layer has requested a resource underneath
  // the path we registered.
  virtual void StartDataRequest(const std::string& path,
                                bool is_off_the_record,
                                int request_id);

  virtual std::string GetMimeType(const std::string&) const;

  std::vector<unsigned char> GetScreenshot(const std::string& path);

 private:
  virtual ~DOMUIScreenshotSource();

//  scoped_refptr<RefCountedBytes> current_screenshot_;
  std::vector<unsigned char> current_screenshot_;
  DISALLOW_COPY_AND_ASSIGN(DOMUIScreenshotSource);
};

#endif  // CHROME_BROWSER_DOM_UI_DOM_UI_SCREENSHOT_SOURCE_H_
