// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PREFS_COMMAND_LINE_PREF_STORE_H_
#define CHROME_BROWSER_PREFS_COMMAND_LINE_PREF_STORE_H_
#pragma once

#include "base/basictypes.h"
#include "base/command_line.h"
#include "base/scoped_ptr.h"
#include "chrome/common/pref_store.h"

class DictionaryValue;

// This PrefStore keeps track of preferences set by command-line switches,
// such as proxy settings.
class CommandLinePrefStore : public PrefStore {
 public:
  explicit CommandLinePrefStore(const CommandLine* command_line);
  virtual ~CommandLinePrefStore() {}

  // PrefStore methods:
  virtual PrefReadError ReadPrefs();
  virtual DictionaryValue* prefs() { return prefs_.get(); }

 protected:
  // Logs a message and returns false if the proxy switches are
  // self-contradictory. Protected so it can be used in unit testing.
  bool ValidateProxySwitches();

 private:
  // Weak reference.
  const CommandLine* command_line_;

  scoped_ptr<DictionaryValue> prefs_;

  struct StringSwitchToPreferenceMapEntry {
    const char* switch_name;
    const char* preference_path;
  };
  static const StringSwitchToPreferenceMapEntry string_switch_map_[];

  // |set_value| indicates what the preference should be set to if the switch
  // is present.
  struct BooleanSwitchToPreferenceMapEntry {
    const char* switch_name;
    const char* preference_path;
    bool set_value;
  };
  static const BooleanSwitchToPreferenceMapEntry boolean_switch_map_[];

  // Using the string and boolean maps, apply command-line switches to their
  // corresponding preferences in this pref store.
  void ApplySimpleSwitches();

  DISALLOW_COPY_AND_ASSIGN(CommandLinePrefStore);
};

#endif  // CHROME_BROWSER_PREFS_COMMAND_LINE_PREF_STORE_H_
