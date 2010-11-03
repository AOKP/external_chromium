// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DOM_UI_OPTIONS_OPTIONS_UI_H_
#define CHROME_BROWSER_DOM_UI_OPTIONS_OPTIONS_UI_H_
#pragma once

#include <string>

#include "base/scoped_ptr.h"
#include "chrome/browser/dom_ui/chrome_url_data_manager.h"
#include "chrome/browser/dom_ui/dom_ui.h"
#include "chrome/common/notification_observer.h"
#include "chrome/common/notification_registrar.h"
#include "chrome/common/notification_type.h"

class GURL;
class PrefService;
struct UserMetricsAction;

class OptionsUIHTMLSource : public ChromeURLDataManager::DataSource {
 public:
  // The constructor takes over ownership of |localized_strings|.
  explicit OptionsUIHTMLSource(DictionaryValue* localized_strings);
  virtual ~OptionsUIHTMLSource();

  // Called when the network layer has requested a resource underneath
  // the path we registered.
  virtual void StartDataRequest(const std::string& path,
                                bool is_off_the_record,
                                int request_id);
  virtual std::string GetMimeType(const std::string&) const;

 private:
   // Localized strings collection.
  scoped_ptr<DictionaryValue> localized_strings_;

  DISALLOW_COPY_AND_ASSIGN(OptionsUIHTMLSource);
};

// The base class handler of Javascript messages of options pages.
class OptionsPageUIHandler : public DOMMessageHandler,
                             public NotificationObserver {
 public:
  OptionsPageUIHandler();
  virtual ~OptionsPageUIHandler();

  // Is this handler enabled?
  virtual bool IsEnabled() { return true; }

  // Collects localized strings for options page.
  virtual void GetLocalizedValues(DictionaryValue* localized_strings) = 0;

  // Initialize the page.  Called once the DOM is available for manipulation.
  virtual void Initialize() {}

  // Uninitializes the page.  Called just before the object is destructed.
  virtual void Uninitialize() {}

  // DOMMessageHandler implementation.
  virtual void RegisterMessages() {}

  // NotificationObserver implementation.
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details) {}

  void UserMetricsRecordAction(const UserMetricsAction& action);

 protected:
  NotificationRegistrar registrar_;

 private:
  DISALLOW_COPY_AND_ASSIGN(OptionsPageUIHandler);
};

class OptionsUI : public DOMUI {
 public:
  explicit OptionsUI(TabContents* contents);
  virtual ~OptionsUI();

  static RefCountedMemory* GetFaviconResourceBytes();
  void RenderViewCreated(RenderViewHost* render_view_host);

  void InitializeHandlers();

 private:
  // Adds OptionsPageUiHandler to the handlers list if handler is enabled.
  void AddOptionsPageUIHandler(DictionaryValue* localized_strings,
                               OptionsPageUIHandler* handler);

  DISALLOW_COPY_AND_ASSIGN(OptionsUI);
};

#endif  // CHROME_BROWSER_DOM_UI_OPTIONS_OPTIONS_UI_H_
