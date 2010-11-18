// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_DOM_UI_SYSTEM_SETTINGS_PROVIDER_H_
#define CHROME_BROWSER_CHROMEOS_DOM_UI_SYSTEM_SETTINGS_PROVIDER_H_

#include <vector>

#include "base/string16.h"
#include "chrome/browser/chromeos/cros/system_library.h"
#include "chrome/browser/chromeos/cros_settings_provider.h"
#include "third_party/icu/public/i18n/unicode/timezone.h"

class Value;
class ListValue;

namespace chromeos {

class SystemSettingsProvider : public CrosSettingsProvider,
                               public SystemLibrary::Observer {
 public:
  SystemSettingsProvider();
  virtual ~SystemSettingsProvider();

  // CrosSettingsProvider overrides.
  virtual bool Get(const std::string& path, Value** out_value) const;
  virtual bool HandlesSetting(const std::string& path);

  // Overridden from SystemLibrary::Observer:
  virtual void TimezoneChanged(const icu::TimeZone& timezone);

  // Creates the map of timezones used by the options page.
  ListValue* GetTimezoneList();

 private:
  // CrosSettingsProvider overrides.
  virtual void DoSet(const std::string& path, Value* in_value);

  // Gets timezone name.
  static string16 GetTimezoneName(const icu::TimeZone& timezone);

  // Gets timezone ID which is also used as timezone pref value.
  static string16 GetTimezoneID(const icu::TimeZone& timezone);

  // Gets timezone object from its id.
  const icu::TimeZone* GetTimezone(const string16& timezone_id);

  // Timezones.
  std::vector<icu::TimeZone*> timezones_;

  DISALLOW_COPY_AND_ASSIGN(SystemSettingsProvider);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_DOM_UI_SYSTEM_SETTINGS_PROVIDER_H_
