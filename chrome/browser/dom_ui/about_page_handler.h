// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DOM_UI_ABOUT_PAGE_HANDLER_H_
#define CHROME_BROWSER_DOM_UI_ABOUT_PAGE_HANDLER_H_

#include <string>

#include "chrome/browser/dom_ui/options_ui.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/cros/update_library.h"
#include "chrome/browser/chromeos/version_loader.h"
#endif

// ChromeOS about page UI handler.
class AboutPageHandler : public OptionsPageUIHandler {
 public:
  AboutPageHandler();
  virtual ~AboutPageHandler();

  // OptionsUIHandler implementation.
  virtual void GetLocalizedValues(DictionaryValue* localized_strings);
  virtual void RegisterMessages();

 private:

  void PageReady(const ListValue* args);

#if defined(OS_CHROMEOS)
  void CheckNow(const ListValue* args);
  // Callback from chromeos::VersionLoader giving the version.
  void OnOSVersion(chromeos::VersionLoader::Handle handle,
                   std::string version);
  void UpdateStatus(const chromeos::UpdateLibrary::Status& status);

  // Handles asynchronously loading the version.
  chromeos::VersionLoader loader_;

  // Used to request the version.
  CancelableRequestConsumer consumer_;

  // Update Observer
  class UpdateObserver;
  scoped_ptr<UpdateObserver> update_observer_;

  int progress_;
  bool sticky_;
  bool started_;
#endif

  DISALLOW_COPY_AND_ASSIGN(AboutPageHandler);
};

#endif  // CHROME_BROWSER_DOM_UI_ABOUT_PAGE_HANDLER_H_

