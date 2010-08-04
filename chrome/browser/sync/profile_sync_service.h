// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_PROFILE_SYNC_SERVICE_H_
#define CHROME_BROWSER_SYNC_PROFILE_SYNC_SERVICE_H_

#include <string>
#include <map>

#include "base/basictypes.h"
#include "base/gtest_prod_util.h"
#include "base/observer_list.h"
#include "base/scoped_ptr.h"
#include "base/time.h"
#include "chrome/browser/google_service_auth_error.h"
#include "chrome/browser/pref_member.h"
#include "chrome/browser/sync/glue/data_type_controller.h"
#include "chrome/browser/sync/glue/data_type_manager.h"
#include "chrome/browser/sync/glue/sync_backend_host.h"
#include "chrome/browser/sync/notification_method.h"
#include "chrome/browser/sync/profile_sync_service_observer.h"
#include "chrome/browser/sync/sync_setup_wizard.h"
#include "chrome/browser/sync/syncable/model_type.h"
#include "chrome/browser/sync/unrecoverable_error_handler.h"
#include "chrome/common/notification_observer.h"
#include "chrome/common/notification_registrar.h"
#include "googleurl/src/gurl.h"

class NotificationDetails;
class NotificationSource;
class NotificationType;
class Profile;
class ProfileSyncFactory;

// ProfileSyncService is the layer between browser subsystems like bookmarks,
// and the sync backend.  Each subsystem is logically thought of as being
// a sync datatype.
//
// Individual datatypes can, at any point, be in a variety of stages of being
// "enabled".  Here are some specific terms for concepts used in this class:
//
//   'Registered' (feature suppression for a datatype)
//
//      When a datatype is registered, the user has the option of syncing it.
//      The sync opt-in UI will show only registered types; a checkbox should
//      never be shown for an unregistered type, and nor should it ever be
//      synced.
//
//      A datatype is considered registered once RegisterDataTypeController
//      has been called with that datatype's DataTypeController.
//
//   'Preferred' (user preferences and opt-out for a datatype)
//
//      This means the user's opt-in or opt-out preference on a per-datatype
//      basis.  The sync service will try to make active exactly these types.
//      If a user has opted out of syncing a particular datatype, it will
//      be registered, but not preferred.
//
//      This state is controlled by the ConfigurePreferredDataTypes and
//      GetPreferredDataTypes.  They are stored in the preferences system,
//      and persist; though if a datatype is not registered, it cannot
//      be a preferred datatype.
//
//   'Active' (run-time initialization of sync system for a datatype)
//
//      An active datatype is a preferred datatype that is actively being
//      synchronized: the syncer has been instructed to querying the server
//      for this datatype, first-time merges have finished, and there is an
//      actively installed ChangeProcessor that listens for changes to this
//      datatype, propagating such changes into and out of the sync backend
//      as necessary.
//
//      When a datatype is in the process of becoming active, it may be
//      in some intermediate state.  Those finer-grained intermediate states
//      are differentiated by the DataTypeController state.
//
class ProfileSyncService : public browser_sync::SyncFrontend,
                           public browser_sync::UnrecoverableErrorHandler,
                           public NotificationObserver {
 public:
  typedef ProfileSyncServiceObserver Observer;
  typedef browser_sync::SyncBackendHost::Status Status;

  enum SyncEventCodes  {
    MIN_SYNC_EVENT_CODE = 0,

    // Events starting the sync service.
    START_FROM_NTP = 1,      // Sync was started from the ad in NTP
    START_FROM_WRENCH = 2,   // Sync was started from the Wrench menu.
    START_FROM_OPTIONS = 3,  // Sync was started from Wrench->Options.
    START_FROM_BOOKMARK_MANAGER = 4,  // Sync was started from Bookmark manager.

    // Events regarding cancellation of the signon process of sync.
    CANCEL_FROM_SIGNON_WITHOUT_AUTH = 10,   // Cancelled before submitting
                                            // username and password.
    CANCEL_DURING_SIGNON = 11,              // Cancelled after auth.
    CANCEL_FROM_CHOOSE_DATA_TYPES = 12,     // Cancelled before choosing data
                                            // types and clicking OK.
    // Events resulting in the stoppage of sync service.
    STOP_FROM_OPTIONS = 20,  // Sync was stopped from Wrench->Options.

    // Miscellaneous events caused by sync service.

    MAX_SYNC_EVENT_CODE
  };

  // Default sync server URL.
  static const char* kSyncServerUrl;
  // Sync server URL for dev channel users
  static const char* kDevServerUrl;

  ProfileSyncService(ProfileSyncFactory* factory_,
                     Profile* profile,
                     bool bootstrap_sync_authentication);
  virtual ~ProfileSyncService();

  // Initializes the object. This should be called every time an object of this
  // class is constructed.
  void Initialize();

  // Registers a data type controller with the sync service.  This
  // makes the data type controller available for use, it does not
  // enable or activate the synchronization of the data type (see
  // ActivateDataType).  Takes ownership of the pointer.
  void RegisterDataTypeController(
      browser_sync::DataTypeController* data_type_controller);

  // Fills state_map with a map of current data types that are possible to
  // sync, as well as their states.
  void GetDataTypeControllerStates(
    browser_sync::DataTypeController::StateMap* state_map) const;

  // Enables/disables sync for user.
  virtual void EnableForUser(gfx::NativeWindow parent_window);
  virtual void DisableForUser();

  // Whether sync is enabled by user or not.
  virtual bool HasSyncSetupCompleted() const;
  void SetSyncSetupCompleted();

  // SyncFrontend implementation.
  virtual void OnBackendInitialized();
  virtual void OnSyncCycleCompleted();
  virtual void OnAuthError();
  virtual void OnStopSyncingPermanently();

  // Called when a user enters credentials through UI.
  virtual void OnUserSubmittedAuth(const std::string& username,
                                   const std::string& password,
                                   const std::string& captcha);

  // Called when a user chooses which data types to sync as part of the sync
  // setup wizard.  |sync_everything| represents whether they chose the
  // "keep everything synced" option; if true, |chosen_types| will be ignored
  // and all data types will be synced.  |sync_everything| means "sync all
  // current and future data types."
  virtual void OnUserChoseDatatypes(bool sync_everything,
      const syncable::ModelTypeSet& chosen_types);

  // Called when a user cancels any setup dialog (login, etc).
  virtual void OnUserCancelledDialog();

  // Get various information for displaying in the user interface.
  browser_sync::SyncBackendHost::StatusSummary QuerySyncStatusSummary();
  browser_sync::SyncBackendHost::Status QueryDetailedSyncStatus();

  const GoogleServiceAuthError& GetAuthError() const {
    return last_auth_error_;
  }

  // Displays a dialog for the user to enter GAIA credentials and attempt
  // re-authentication, and returns true if it actually opened the dialog.
  // Returns false if a dialog is already showing, an auth attempt is in
  // progress, the sync system is already authenticated, or some error
  // occurred preventing the action. We make it the duty of ProfileSyncService
  // to open the dialog to easily ensure only one is ever showing.
  bool SetupInProgress() const {
    return !HasSyncSetupCompleted() &&
        (WizardIsVisible() || bootstrap_sync_authentication_);
  }
  bool WizardIsVisible() const {
    return wizard_.IsVisible();
  }
  void ShowLoginDialog(gfx::NativeWindow parent_window);

  void ShowChooseDataTypes(gfx::NativeWindow parent_window);

  // Pretty-printed strings for a given StatusSummary.
  static std::wstring BuildSyncStatusSummaryText(
      const browser_sync::SyncBackendHost::StatusSummary& summary);

  // Returns true if the SyncBackendHost has told us it's ready to accept
  // changes.
  // TODO(timsteele): What happens if the bookmark model is loaded, a change
  // takes place, and the backend isn't initialized yet?
  bool sync_initialized() const { return backend_initialized_; }
  bool unrecoverable_error_detected() const {
    return unrecoverable_error_detected_;
  }
  const std::string& unrecoverable_error_message() {
    return unrecoverable_error_message_;
  }
  tracked_objects::Location unrecoverable_error_location() {
    return unrecoverable_error_location_.get() ?
        *unrecoverable_error_location_.get() : tracked_objects::Location();
  }

  bool UIShouldDepictAuthInProgress() const {
    return is_auth_in_progress_;
  }

  // A timestamp marking the last time the service observed a transition from
  // the SYNCING state to the READY state. Note that this does not reflect the
  // last time we polled the server to see if there were any changes; the
  // timestamp is only snapped when syncing takes place and we download or
  // upload some bookmark entity.
  const base::Time& last_synced_time() const { return last_synced_time_; }

  // Returns a user-friendly string form of last synced time (in minutes).
  std::wstring GetLastSyncedTimeString() const;

  // Returns the authenticated username of the sync user, or empty if none
  // exists. It will only exist if the authentication service provider (e.g
  // GAIA) has confirmed the username is authentic.
  virtual string16 GetAuthenticatedUsername() const;

  const std::string& last_attempted_user_email() const {
    return last_attempted_user_email_;
  }

  // The profile we are syncing for.
  Profile* profile() { return profile_; }

  // Adds/removes an observer. ProfileSyncService does not take ownership of
  // the observer.
  virtual void AddObserver(Observer* observer);
  virtual void RemoveObserver(Observer* observer);

  // Record stats on various events.
  static void SyncEvent(SyncEventCodes code);

  // Returns whether sync is enabled.  Sync can be enabled/disabled both
  // at compile time (e.g., on a per-OS basis) or at run time (e.g.,
  // command-line switches).
  static bool IsSyncEnabled();

  // Retuns whether sync is managed, i.e. controlled by configuration
  // management. If so, the user is not allowed to configure sync.
  bool IsManaged();

  // UnrecoverableErrorHandler implementation.
  virtual void OnUnrecoverableError(
      const tracked_objects::Location& from_here,
      const std::string& message);

  browser_sync::SyncBackendHost* backend() { return backend_.get(); }

  virtual void ActivateDataType(
      browser_sync::DataTypeController* data_type_controller,
      browser_sync::ChangeProcessor* change_processor);
  virtual void DeactivateDataType(
      browser_sync::DataTypeController* data_type_controller,
      browser_sync::ChangeProcessor* change_processor);

  // NotificationObserver implementation.
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

  // Changes which data types we're going to be syncing to |preferred_types|.
  // If it is running, the DataTypeManager will be instructed to reconfigure
  // the sync backend so that exactly these datatypes are actively synced.  See
  // class comment for more on what it means for a datatype to be Preferred.
  virtual void ChangePreferredDataTypes(
      const syncable::ModelTypeSet& preferred_types);

  // Get the set of currently enabled data types (as chosen or configured by
  // the user).  See class comment for more on what it means for a datatype
  // to be Preferred.
  virtual void GetPreferredDataTypes(
      syncable::ModelTypeSet* preferred_types) const;

  // Gets the set of all data types that could be allowed (the set that
  // should be advertised to the user).  These will typically only change
  // via a command-line option.  See class comment for more on what it means
  // for a datatype to be Registered.
  virtual void GetRegisteredDataTypes(
      syncable::ModelTypeSet* registered_types) const;

  // Checks whether the Cryptographer is ready to encrypt and decrypt updates
  // for sensitive data types.
  virtual bool IsCryptographerReady() const;

  // Sets the Cryptographer's passphrase. This will check asynchronously whether
  // the passphrase is valid and notify ProfileSyncServiceObservers via the
  // NotificationService when the outcome is known.
  virtual void SetPassphrase(const std::string& passphrase);

 protected:
  // Used by ProfileSyncServiceMock only.
  //
  // TODO(akalin): Separate this class out into an abstract
  // ProfileSyncService interface and a ProfileSyncServiceImpl class
  // so we don't need this hack anymore.
  ProfileSyncService();

  // Helper to install and configure a data type manager.
  void ConfigureDataTypeManager();

  // Returns whether processing changes is allowed.  Check this before doing
  // any model-modifying operations.
  bool ShouldPushChanges();

  // Starts up the backend sync components.
  void StartUp();
  // Shuts down the backend sync components.
  // |sync_disabled| indicates if syncing is being disabled or not.
  void Shutdown(bool sync_disabled);

  // Methods to register and remove preferences.
  void RegisterPreferences();
  void ClearPreferences();

  // Tests need to override this.  If |delete_sync_data_folder| is true, then
  // this method will delete all previous "Sync Data" folders. (useful if the
  // folder is partial/corrupt)
  virtual void InitializeBackend(bool delete_sync_data_folder);

  const browser_sync::DataTypeController::TypeMap& data_type_controllers() {
    return data_type_controllers_;
  }

  // We keep track of the last auth error observed so we can cover up the first
  // "expected" auth failure from observers.
  // TODO(timsteele): Same as expecting_first_run_auth_needed_event_. Remove
  // this!
  GoogleServiceAuthError last_auth_error_;

  // Our asynchronous backend to communicate with sync components living on
  // other threads.
  scoped_ptr<browser_sync::SyncBackendHost> backend_;

  // Cache of the last name the client attempted to authenticate.
  std::string last_attempted_user_email_;

 private:
  friend class ProfileSyncServiceTest;
  friend class ProfileSyncServicePreferenceTest;
  friend class ProfileSyncServiceTestHarness;
  FRIEND_TEST_ALL_PREFIXES(ProfileSyncServiceTest, InitialState);
  FRIEND_TEST_ALL_PREFIXES(ProfileSyncServiceTest,
                           UnrecoverableErrorSuspendsService);

  // Initializes the various settings from the command line.
  void InitSettings();

  // Sets the last synced time to the current time.
  void UpdateLastSyncedTime();

  static const wchar_t* GetPrefNameForDataType(syncable::ModelType data_type);

  // Time at which we begin an attempt a GAIA authorization.
  base::TimeTicks auth_start_time_;

  // Time at which error UI is presented for the new tab page.
  base::TimeTicks auth_error_time_;

  // Factory used to create various dependent objects.
  ProfileSyncFactory* factory_;

  // The profile whose data we are synchronizing.
  Profile* profile_;

  // True if the profile sync service should attempt to use an LSID
  // cookie for authentication.  This is typically set to true in
  // ChromiumOS since we want to use the system level authentication
  // for sync.
  bool bootstrap_sync_authentication_;

  // TODO(ncarter): Put this in a profile, once there is UI for it.
  // This specifies where to find the sync server.
  GURL sync_service_url_;

  // The last time we detected a successful transition from SYNCING state.
  // Our backend notifies us whenever we should take a new snapshot.
  base::Time last_synced_time_;

  // List of available data type controllers.
  browser_sync::DataTypeController::TypeMap data_type_controllers_;

  // Whether the SyncBackendHost has been initialized.
  bool backend_initialized_;

  // Set to true when the user first enables sync, and we are waiting for
  // syncapi to give us the green light on providing credentials for the first
  // time. It is set back to false as soon as we get this message, and is
  // false all other times so we don't have to persist this value as it will
  // get initialized to false.
  // TODO(timsteele): Remove this by way of starting the wizard when enabling
  // sync *before* initializing the backend. syncapi will need to change, but
  // it means we don't have to wait for the first AuthError; if we ever get
  // one, it is actually an error and this bool isn't needed.
  bool expecting_first_run_auth_needed_event_;

  // Various pieces of UI query this value to determine if they should show
  // an "Authenticating.." type of message.  We are the only central place
  // all auth attempts funnel through, so it makes sense to provide this.
  // As its name suggests, this should NOT be used for anything other than UI.
  bool is_auth_in_progress_;

  SyncSetupWizard wizard_;

  // True if an unrecoverable error (e.g. violation of an assumed invariant)
  // occurred during syncer operation.  This value should be checked before
  // doing any work that might corrupt things further.
  bool unrecoverable_error_detected_;

  // A message sent when an unrecoverable error occurred.
  std::string unrecoverable_error_message_;
  scoped_ptr<tracked_objects::Location> unrecoverable_error_location_;

  // Whether to use the (new, untested) Chrome-socket-based
  // buzz::AsyncSocket implementation for notifications.
  bool use_chrome_async_socket_;

  // Which peer-to-peer notification method to use.
  browser_sync::NotificationMethod notification_method_;

  // Manages the start and stop of the various data types.
  scoped_ptr<browser_sync::DataTypeManager> data_type_manager_;

  ObserverList<Observer> observers_;

  NotificationRegistrar registrar_;

  ScopedRunnableMethodFactory<ProfileSyncService>
    scoped_runnable_method_factory_;

  // The preference that controls whether sync is under control by configuration
  // management.
  BooleanPrefMember pref_sync_managed_;

  // This allows us to gracefully handle an ABORTED return code from the
  // DataTypeManager in the event that the server informed us to cease and
  // desist syncing immediately.
  bool expect_sync_configuration_aborted_;

  DISALLOW_COPY_AND_ASSIGN(ProfileSyncService);
};

#endif  // CHROME_BROWSER_SYNC_PROFILE_SYNC_SERVICE_H_
