// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DOM_UI_CORE_OPTIONS_HANDLER_H_
#define CHROME_BROWSER_DOM_UI_CORE_OPTIONS_HANDLER_H_
#pragma once

#include <map>
#include <string>

#include "base/values.h"
#include "chrome/browser/dom_ui/options_ui.h"
#include "chrome/browser/prefs/pref_change_registrar.h"

// Core options UI handler.
// Handles resource and JS calls common to all options sub-pages.
class CoreOptionsHandler : public OptionsPageUIHandler {
 public:
  CoreOptionsHandler();

  // OptionsUIHandler implementation.
  virtual void GetLocalizedValues(DictionaryValue* localized_strings);
  virtual void Uninitialize();

  // NotificationObserver implementation.
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

  // DOMMessageHandler implementation.
  virtual void RegisterMessages();
  virtual DOMMessageHandler* Attach(DOMUI* dom_ui);

 protected:
  // Fetches a pref value of given |pref_name|.
  // Note that caller owns the returned Value.
  virtual Value* FetchPref(const std::string& pref_name);

  // Observes a pref of given |pref_name|.
  virtual void ObservePref(const std::string& pref_name);

  // Sets a pref value |value_string| of |pref_type| to given |pref_name|.
  virtual void SetPref(const std::string& pref_name,
                       Value::ValueType pref_type,
                       const std::string& value_string,
                       const std::string& metric);

  // Stops observing given preference identified by |path|.
  virtual void StopObservingPref(const std::string& path);

  // Records a user metric action for the given value.
  void ProcessUserMetric(Value::ValueType pref_type,
                         const std::string& value_string,
                         const std::string& metric);

  typedef std::multimap<std::string, std::wstring> PreferenceCallbackMap;
  PreferenceCallbackMap pref_callback_map_;
 private:
  // Callback for the "coreOptionsInitialize" message.  This message will
  // trigger the Initialize() method of all other handlers so that final
  // setup can be performed before the page is shown.
  void HandleInitialize(const ListValue* args);

  // Callback for the "fetchPrefs" message. This message accepts the list of
  // preference names passed as |value| parameter (ListValue). It passes results
  // dictionary of preference values by calling prefsFetched() JS method on the
  // page.
  void HandleFetchPrefs(const ListValue* args);

  // Callback for the "observePrefs" message. This message initiates
  // notification observing for given array of preference names.
  void HandleObservePrefs(const ListValue* args);

  // Callbacks for the "set<type>Pref" message. This message saves the new
  // preference value. The input value is an array of strings representing
  // name-value preference pair.
  void HandleSetBooleanPref(const ListValue* args);
  void HandleSetIntegerPref(const ListValue* args);
  void HandleSetStringPref(const ListValue* args);
  void HandleSetObjectPref(const ListValue* args);

  void HandleSetPref(const ListValue* args, Value::ValueType type);

  // Callback for the "coreOptionsUserMetricsAction" message.  This records
  // an action that should be tracked if metrics recording is enabled.
  void HandleUserMetricsAction(const ListValue* args);

  void NotifyPrefChanged(const std::string* pref_name);

  PrefChangeRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(CoreOptionsHandler);
};

#endif  // CHROME_BROWSER_DOM_UI_CORE_OPTIONS_HANDLER_H_
