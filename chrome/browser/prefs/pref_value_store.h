// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PREFS_PREF_VALUE_STORE_H_
#define CHROME_BROWSER_PREFS_PREF_VALUE_STORE_H_
#pragma once

#include <map>
#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/callback.h"
#include "base/gtest_prod_util.h"
#include "base/ref_counted.h"
#include "base/scoped_ptr.h"
#include "base/values.h"
#include "chrome/browser/chrome_thread.h"
#include "chrome/browser/prefs/pref_notifier.h"
#include "chrome/common/pref_store.h"

class FilePath;
class PrefStore;
class Profile;

// The PrefValueStore manages various sources of values for Preferences
// (e.g., configuration policies, extensions, and user settings). It returns
// the value of a Preference from the source with the highest priority, and
// allows setting user-defined values for preferences that are not managed.
// See PrefNotifier for a list of the available preference sources (PrefStores)
// and their descriptions.
//
// Unless otherwise explicitly noted, all of the methods of this class must
// be called on the UI thread.
class PrefValueStore : public base::RefCountedThreadSafe<PrefValueStore> {
 public:
  // Returns a new PrefValueStore with all applicable PrefStores. The
  // |pref_filename| points to the user preference file. The |profile| is the
  // one to which these preferences apply; it may be NULL if we're dealing
  // with the local state. If |pref_filename| is empty, the user PrefStore will
  // not be created. If |user_only| is true, no PrefStores will be created
  // other than the user and default PrefStores. This should not normally be
  // called directly: the usual way to create a PrefValueStore is by creating a
  // PrefService.
  static PrefValueStore* CreatePrefValueStore(const FilePath& pref_filename,
                                              Profile* profile,
                                              bool user_only);

  ~PrefValueStore();

  // Gets the value for the given preference name that has a valid value type;
  // that is, the same type the preference was registered with, or NULL for
  // default values of Dictionaries and Lists. Returns true if a valid value
  // was found in any of the available PrefStores. Most callers should use
  // Preference::GetValue() instead of calling this method directly.
  bool GetValue(const std::string& name, Value** out_value) const;

  // Adds a preference to the mapping of names to types.
  void RegisterPreferenceType(const std::string& name, Value::ValueType type);

  // Gets the registered value type for the given preference name. Returns
  // Value::TYPE_NULL if the preference has never been registered.
  Value::ValueType GetRegisteredType(const std::string& name) const;

  // Read preference values into the three PrefStores so that they are available
  // through the GetValue method. Return the first error that occurs (but
  // continue reading the remaining PrefStores).
  PrefStore::PrefReadError ReadPrefs();

  // Persists prefs (to disk or elsewhere). Returns true if writing values was
  // successful. In practice, only the user prefs are expected to be written
  // out.
  bool WritePrefs();

  // Calls the method ScheduleWritePrefs on the PrefStores. In practice, only
  // the user prefs are expected to be written out.
  void ScheduleWritePrefs();

  // Returns true if the PrefValueStore contains the given preference (i.e.,
  // it's been registered), and a value with the correct type has been actively
  // set in some pref store. The application default specified when the pref was
  // registered does not count as an "actively set" value, but another pref
  // store setting a value that happens to be equal to the default does.
  bool HasPrefPath(const char* name) const;

  // Called by the PrefNotifier when the value of the preference at |path| has
  // changed, been added, or been removed in one of the PrefStores. The
  // |new_store| is the PrefStoreType of the caller. Returns true if the
  // effective value of the preference has changed, or if the store controlling
  // the pref has changed. Virtual so it can be mocked for a unit test.
  virtual bool PrefHasChanged(const char* path,
                              PrefNotifier::PrefStoreType new_store);

  // Returns true if the PrefValueStore is read-only.
  // Because the managed and recommended PrefStores are always read-only, the
  // PrefValueStore as a whole is read-only if the PrefStore containing the user
  // preferences is read-only.
  bool ReadOnly();

  // Alters the user-defined value of a preference. Even if the preference is
  // managed this method allows the user-defined value of the preference to be
  // set. But GetValue calls will not return this value as long as the
  // preference is managed. Instead GetValue will return the managed value
  // of the preference. Note that the PrefValueStore takes the ownership of
  // the value referenced by |in_value|. It is an error to call this when no
  // user PrefStore has been set. Returns true if the user-set value of the
  // preference was newly added or changed.
  bool SetUserPrefValue(const char* name, Value* in_value);

  // Removes a value from the user PrefStore. If a preference is managed
  // this function should have no visible effect. Returns true if there was a
  // user-set value to be removed.
  bool RemoveUserPrefValue(const char* name);

  // Sets a value in the DefaultPrefStore, which takes ownership of the Value.
  void SetDefaultPrefValue(const char* name, Value* in_value);

  // These methods return true if a preference with the given name is in the
  // indicated pref store, even if that value is currently being overridden by
  // a higher-priority source.
  bool PrefValueInManagedStore(const char* name) const;
  bool PrefValueInExtensionStore(const char* name) const;
  bool PrefValueInUserStore(const char* name) const;

  // These methods return true if a preference with the given name is actually
  // being controlled by the indicated pref store and not being overridden by
  // a higher-priority source.
  bool PrefValueFromExtensionStore(const char* name) const;
  bool PrefValueFromUserStore(const char* name) const;
  bool PrefValueFromDefaultStore(const char* name) const;

  // Check whether a Preference value is modifiable by the user, i.e. whether
  // there is no higher-priority source controlling it.
  bool PrefValueUserModifiable(const char* name) const;

  // Returns the pref store type identifying the source that controls the
  // Preference identified by |name|. If none of the sources has a value,
  // PrefNotifier::INVALID_STORE is returned. In practice, the default PrefStore
  // should always have a value for any registered preferencem, so INVALID_STORE
  // indicates an error.
  PrefNotifier::PrefStoreType ControllingPrefStoreForPref(
      const char* name) const;

  // Signature of callback triggered after policy refresh. Parameter is not
  // passed as reference to prevent passing along a pointer to a set whose
  // lifecycle is managed in another thread.
  typedef Callback1<std::vector<std::string> >::Type AfterRefreshCallback;

  // Called as a result of a notification of policy change. Triggers a
  // reload of managed preferences from policy. Caller must pass in
  // new, uninitialized managed and recommended PrefStores in
  // |managed_pref_store| and |recommended_pref_store| respectively, since
  // PrefValueStore doesn't know about policy-specific PrefStores.
  // |callback| is called with the set of preferences changed by the policy
  // refresh. |callback| is called on the caller's thread as a Task
  // after RefreshPolicyPrefs has returned. RefreshPolicyPrefs takes ownership
  // of the |callback| object.
  void RefreshPolicyPrefs(PrefStore* managed_pref_store,
                          PrefStore* recommended_pref_store,
                          AfterRefreshCallback* callback);

 protected:
  // In decreasing order of precedence:
  //   |managed_prefs| contains all managed (policy) preference values.
  //   |extension_prefs| contains preference values set by extensions.
  //   |command_line_prefs| contains preference values set by command-line
  //        switches.
  //   |user_prefs| contains all user-set preference values.
  //   |recommended_prefs| contains all recommended (policy) preference values.
  //   |default_prefs| contains application-default preference values. It must
  //        be non-null if any preferences are to be registered.
  //
  // This constructor should only be used internally, or by subclasses in
  // testing. The usual way to create a PrefValueStore is by creating a
  // PrefService.
  PrefValueStore(PrefStore* managed_prefs,
                 PrefStore* extension_prefs,
                 PrefStore* command_line_prefs,
                 PrefStore* user_prefs,
                 PrefStore* recommended_prefs,
                 PrefStore* default_prefs);

 private:
  friend class PrefValueStoreTest;
  FRIEND_TEST_ALL_PREFIXES(PrefValueStoreTest,
                           TestRefreshPolicyPrefsCompletion);

  scoped_ptr<PrefStore> pref_stores_[PrefNotifier::PREF_STORE_TYPE_MAX + 1];

  // A mapping of preference names to their registered types.
  typedef std::map<std::string, Value::ValueType> PrefTypeMap;
  PrefTypeMap pref_types_;

  // Returns true if the preference with the given name has a value in the
  // given PrefStoreType, of the same value type as the preference was
  // registered with.
  bool PrefValueInStore(const char* name,
                        PrefNotifier::PrefStoreType store) const;

  // Called during policy refresh after ReadPrefs completes on the thread
  // that initiated the policy refresh. RefreshPolicyPrefsCompletion takes
  // ownership of the |callback| object.
  void RefreshPolicyPrefsCompletion(
      PrefStore* new_managed_pref_store,
      PrefStore* new_recommended_pref_store,
      AfterRefreshCallback* callback);

  // Called during policy refresh to do the ReadPrefs on the FILE thread.
  // RefreshPolicyPrefsOnFileThread takes ownership of the |callback| object.
  void RefreshPolicyPrefsOnFileThread(
      ChromeThread::ID calling_thread_id,
      PrefStore* new_managed_pref_store,
      PrefStore* new_recommended_pref_store,
      AfterRefreshCallback* callback);

  DISALLOW_COPY_AND_ASSIGN(PrefValueStore);
};

#endif  // CHROME_BROWSER_PREFS_PREF_VALUE_STORE_H_
