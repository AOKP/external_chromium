// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CONTENT_SETTINGS_PREF_CONTENT_SETTINGS_PROVIDER_H_
#define CHROME_BROWSER_CONTENT_SETTINGS_PREF_CONTENT_SETTINGS_PROVIDER_H_
#pragma once

// A content settings provider that takes its settings out of the pref service.

#include "base/basictypes.h"
#include "base/lock.h"
#include "chrome/browser/content_settings/content_settings_provider.h"
#include "chrome/browser/prefs/pref_change_registrar.h"
#include "chrome/common/notification_observer.h"
#include "chrome/common/notification_registrar.h"

class ContentSettingsDetails;
class DictionaryValue;
class PrefService;
class Profile;

class PrefContentSettingsProvider : public ContentSettingsProviderInterface,
                                    public NotificationObserver {
 public:
  explicit PrefContentSettingsProvider(Profile* profile);
  virtual ~PrefContentSettingsProvider();

  // ContentSettingsProviderInterface implementation.
  virtual bool CanProvideDefaultSetting(ContentSettingsType content_type) const;
  virtual ContentSetting ProvideDefaultSetting(
      ContentSettingsType content_type) const;
  virtual void UpdateDefaultSetting(ContentSettingsType content_type,
                                    ContentSetting setting);
  virtual void ResetToDefaults();
  virtual bool DefaultSettingIsManaged(ContentSettingsType content_type) const;

  static void RegisterUserPrefs(PrefService* prefs);

  // NotificationObserver implementation.
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

 private:
  // Informs observers that content settings have changed. Make sure that
  // |lock_| is not held when calling this, as listeners will usually call one
  // of the GetSettings functions in response, which would then lead to a
  // mutex deadlock.
  void NotifyObservers(const ContentSettingsDetails& details);

  void UnregisterObservers();

  // Sets the fields of |settings| based on the values in |dictionary|.
  void GetSettingsFromDictionary(const DictionaryValue* dictionary,
                                 ContentSettings* settings);

  // Forces the default settings to be explicitly set instead of themselves
  // being CONTENT_SETTING_DEFAULT.
  void ForceDefaultsToBeExplicit();

  // Reads the default settings from the preferences service. If |overwrite| is
  // true and the preference is missing, the local copy will be cleared as well.
  void ReadDefaultSettings(bool overwrite);

  // Copies of the pref data, so that we can read it on the IO thread.
  ContentSettings default_content_settings_;

  Profile* profile_;

  // Whether this settings map is for an OTR session.
  bool is_off_the_record_;

  // Used around accesses to the default_content_settings_ object to guarantee
  // thread safety.
  mutable Lock lock_;

  PrefChangeRegistrar pref_change_registrar_;
  NotificationRegistrar notification_registrar_;

  // Whether we are currently updating preferences, this is used to ignore
  // notifications from the preferences service that we triggered ourself.
  bool updating_preferences_;

  DISALLOW_COPY_AND_ASSIGN(PrefContentSettingsProvider);
};

#endif  // CHROME_BROWSER_CONTENT_SETTINGS_PREF_CONTENT_SETTINGS_PROVIDER_H_
