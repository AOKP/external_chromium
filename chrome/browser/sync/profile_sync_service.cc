// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/profile_sync_service.h"

#include <map>
#include <set>

#include "app/l10n_util.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/histogram.h"
#include "base/logging.h"
#include "base/stl_util-inl.h"
#include "base/string_util.h"
#include "base/task.h"
#include "chrome/browser/chrome_thread.h"
#include "chrome/browser/history/history_types.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/pref_service.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/net/gaia/token_service.h"
#include "chrome/browser/sync/engine/syncapi.h"
#include "chrome/browser/sync/glue/change_processor.h"
#include "chrome/browser/sync/glue/data_type_controller.h"
#include "chrome/browser/sync/glue/data_type_manager.h"
#include "chrome/browser/sync/profile_sync_factory.h"
#include "chrome/browser/sync/syncable/directory_manager.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/notification_details.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/notification_source.h"
#include "chrome/common/notification_type.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/time_format.h"
#include "grit/generated_resources.h"
#include "net/base/cookie_monster.h"

using browser_sync::ChangeProcessor;
using browser_sync::DataTypeController;
using browser_sync::DataTypeManager;
using browser_sync::SyncBackendHost;

typedef GoogleServiceAuthError AuthError;

const char* ProfileSyncService::kSyncServerUrl =
    "https://clients4.google.com/chrome-sync";

const char* ProfileSyncService::kDevServerUrl =
    "https://clients4.google.com/chrome-sync/dev";

ProfileSyncService::ProfileSyncService(ProfileSyncFactory* factory,
                                       Profile* profile,
                                       bool bootstrap_sync_authentication)
    : last_auth_error_(AuthError::None()),
      factory_(factory),
      profile_(profile),
      bootstrap_sync_authentication_(bootstrap_sync_authentication),
      sync_service_url_(kDevServerUrl),
      backend_initialized_(false),
      expecting_first_run_auth_needed_event_(false),
      is_auth_in_progress_(false),
      ALLOW_THIS_IN_INITIALIZER_LIST(wizard_(this)),
      unrecoverable_error_detected_(false),
      use_chrome_async_socket_(false),
      notification_method_(browser_sync::kDefaultNotificationMethod),
      ALLOW_THIS_IN_INITIALIZER_LIST(scoped_runnable_method_factory_(this)) {
  DCHECK(factory);
  DCHECK(profile);
  registrar_.Add(this,
                 NotificationType::SYNC_CONFIGURE_START,
                 NotificationService::AllSources());
  registrar_.Add(this,
                 NotificationType::SYNC_CONFIGURE_DONE,
                 NotificationService::AllSources());
  registrar_.Add(this,
                 NotificationType::SYNC_PASSPHRASE_REQUIRED,
                 NotificationService::AllSources());
  registrar_.Add(this,
                 NotificationType::SYNC_PASSPHRASE_ACCEPTED,
                 NotificationService::AllSources());

  // By default, dev & chromium users will go to the development servers.
  // Dev servers have more features than standard sync servers.
  // Chrome stable and beta builds will go to the standard sync servers.
#if defined(GOOGLE_CHROME_BUILD)
  // For stable, this is "". For dev, this is "dev". For beta, this is "beta".
  // For daily, this is "canary build".
  // For linux Chromium builds, this could be anything depending on the
  // distribution, so always direct those users to dev server urls.
  // If this is an official build, it will always be one of the above.
  string16 channel = platform_util::GetVersionStringModifier();
  if (channel.empty() || channel == ASCIIToUTF16("beta")) {
    LOG(INFO) << "Detected official build, using official sync server.";
    sync_service_url_ = GURL(kSyncServerUrl);
  } else {
    LOG(INFO) << "Detected official build, but using dev channel sync server.";
  }
#else
  LOG(INFO) << "Unofficial build, using dev channel sync server.";
#endif
}

ProfileSyncService::ProfileSyncService()
    : last_auth_error_(AuthError::None()),
      factory_(NULL),
      profile_(NULL),
      bootstrap_sync_authentication_(false),
      sync_service_url_(kSyncServerUrl),
      backend_initialized_(false),
      expecting_first_run_auth_needed_event_(false),
      is_auth_in_progress_(false),
      ALLOW_THIS_IN_INITIALIZER_LIST(wizard_(this)),
      unrecoverable_error_detected_(false),
      use_chrome_async_socket_(false),
      notification_method_(browser_sync::kDefaultNotificationMethod),
      ALLOW_THIS_IN_INITIALIZER_LIST(scoped_runnable_method_factory_(this)),
      expect_sync_configuration_aborted_(false) {
}

ProfileSyncService::~ProfileSyncService() {
  Shutdown(false);
}

void ProfileSyncService::Initialize() {
  LOG(INFO) << "Starting ProfileSyncService.";
  InitSettings();
  RegisterPreferences();

  // Watch the preference that indicates sync is managed so we can take
  // appropriate action.
  pref_sync_managed_.Init(prefs::kSyncManaged, profile_->GetPrefs(), this);

  // For now, the only thing we can do through policy is to turn sync off.
  if (IsManaged()) {
    DisableForUser();
    return;
  }

  if (!profile()->GetPrefs()->GetBoolean(prefs::kSyncHasSetupCompleted)) {
    DisableForUser();  // Clean up in case of previous crash / setup abort.

    // Automatically start sync in Chromium OS.
    if (bootstrap_sync_authentication_) {
      // If the LSID is empty, we're in a CrOS UI test that is not testing sync
      // behavior, so we don't want the sync service to start.
      if (profile()->GetTokenService() &&
          !profile()->GetTokenService()->HasLsid()) {
        LOG(WARNING) << "Skipping CrOS sync startup, no LSID present.";
        return;
      }
      StartUp();
    }
  } else {
    StartUp();
  }
}

void ProfileSyncService::RegisterDataTypeController(
    DataTypeController* data_type_controller) {
  DCHECK_EQ(data_type_controllers_.count(data_type_controller->type()), 0U);
  data_type_controllers_[data_type_controller->type()] =
      data_type_controller;
}

void ProfileSyncService::GetDataTypeControllerStates(
  browser_sync::DataTypeController::StateMap* state_map) const {
    browser_sync::DataTypeController::TypeMap::const_iterator iter
        = data_type_controllers_.begin();
    for ( ; iter != data_type_controllers_.end(); ++iter ) {
      (*state_map)[iter->first] = iter->second.get()->state();
    }
}

void ProfileSyncService::InitSettings() {
  const CommandLine& command_line = *CommandLine::ForCurrentProcess();

  // Override the sync server URL from the command-line, if sync server
  // command-line argument exists.
  if (command_line.HasSwitch(switches::kSyncServiceURL)) {
    std::string value(command_line.GetSwitchValueASCII(
        switches::kSyncServiceURL));
    if (!value.empty()) {
      GURL custom_sync_url(value);
      if (custom_sync_url.is_valid()) {
        sync_service_url_ = custom_sync_url;
      } else {
        LOG(WARNING) << "The following sync URL specified at the command-line "
                     << "is invalid: " << value;
      }
    }
  }

  LOG(INFO) << "Using " << sync_service_url_ << " for sync server URL.";

  use_chrome_async_socket_ =
      command_line.HasSwitch(switches::kSyncUseChromeAsyncSocket);
  if (use_chrome_async_socket_) {
    LOG(INFO) << "Using ChromeAsyncSocket";
  }

  if (command_line.HasSwitch(switches::kSyncNotificationMethod)) {
    const std::string notification_method_str(
        command_line.GetSwitchValueASCII(switches::kSyncNotificationMethod));
    notification_method_ =
        browser_sync::StringToNotificationMethod(notification_method_str);
  }
}

void ProfileSyncService::RegisterPreferences() {
  PrefService* pref_service = profile_->GetPrefs();
  if (pref_service->FindPreference(prefs::kSyncLastSyncedTime))
    return;
  pref_service->RegisterInt64Pref(prefs::kSyncLastSyncedTime, 0);
  pref_service->RegisterBooleanPref(prefs::kSyncHasSetupCompleted, false);

  // If you've never synced before, or if you're using Chrome OS, all datatypes
  // are on by default.
  // TODO(nick): Perhaps a better model would be to always default to false,
  // and explicitly call SetDataTypes() when the user shows the wizard.
#if defined(OS_CHROMEOS)
  bool enable_by_default = true;
#else
  bool enable_by_default =
      !pref_service->HasPrefPath(prefs::kSyncHasSetupCompleted);
#endif

  pref_service->RegisterBooleanPref(prefs::kSyncBookmarks, true);
  pref_service->RegisterBooleanPref(prefs::kSyncPasswords, enable_by_default);
  pref_service->RegisterBooleanPref(prefs::kSyncPreferences, enable_by_default);
  pref_service->RegisterBooleanPref(prefs::kSyncAutofill, enable_by_default);
  pref_service->RegisterBooleanPref(prefs::kSyncThemes, enable_by_default);
  pref_service->RegisterBooleanPref(prefs::kSyncTypedUrls, enable_by_default);
  pref_service->RegisterBooleanPref(prefs::kSyncExtensions, enable_by_default);

  pref_service->RegisterBooleanPref(prefs::kKeepEverythingSynced,
      enable_by_default);

  pref_service->RegisterBooleanPref(prefs::kSyncManaged, false);
}

void ProfileSyncService::ClearPreferences() {
  PrefService* pref_service = profile_->GetPrefs();
  pref_service->ClearPref(prefs::kSyncLastSyncedTime);
  pref_service->ClearPref(prefs::kSyncHasSetupCompleted);
  // TODO(nick): The current behavior does not clear e.g. prefs::kSyncBookmarks.
  // Is that really what we want?
  pref_service->ScheduleSavePersistentPrefs();
}

void ProfileSyncService::InitializeBackend(bool delete_sync_data_folder) {
  if (!backend_.get()) {
    NOTREACHED();
    return;
  }

  // TODO(akalin): Gather all the command-line-controlled switches
  // into an Options struct to make passing them down less annoying.

  bool invalidate_sync_login = false;
  bool invalidate_sync_xmpp_login = false;
#if !defined(NDEBUG)
  invalidate_sync_login = CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kInvalidateSyncLogin);
  invalidate_sync_xmpp_login = CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kInvalidateSyncXmppLogin);
#endif

  syncable::ModelTypeSet types;
  // If sync setup hasn't finished, we don't want to initialize routing info
  // for any data types so that we don't download updates for types that the
  // user chooses not to sync on the first DownloadUpdatesCommand.
  if (HasSyncSetupCompleted())
    GetPreferredDataTypes(&types);
  backend_->Initialize(sync_service_url_,
                       types,
                       profile_->GetRequestContext(),
                       profile_->GetTokenService()->GetLsid(),
                       delete_sync_data_folder,
                       invalidate_sync_login,
                       invalidate_sync_xmpp_login,
                       use_chrome_async_socket_,
                       notification_method_);
}

void ProfileSyncService::StartUp() {
  // Don't start up multiple times.
  if (backend_.get()) {
    LOG(INFO) << "Skipping bringing up backend host.";
    return;
  }

  LOG(INFO) << "ProfileSyncSerivce bringing up backend host.";

  last_synced_time_ = base::Time::FromInternalValue(
      profile_->GetPrefs()->GetInt64(prefs::kSyncLastSyncedTime));

  backend_.reset(
      new SyncBackendHost(this, profile_, profile_->GetPath(),
                          data_type_controllers_));

  // Initialize the backend.  Every time we start up a new SyncBackendHost,
  // we'll want to start from a fresh SyncDB, so delete any old one that might
  // be there.
  InitializeBackend(!HasSyncSetupCompleted());
}

void ProfileSyncService::Shutdown(bool sync_disabled) {

 // Stop all data type controllers, if needed.
  if (data_type_manager_.get() &&
      data_type_manager_->state() != DataTypeManager::STOPPED) {
    data_type_manager_->Stop();
  }

  data_type_manager_.reset();

  // Move aside the backend so nobody else tries to use it while we are
  // shutting it down.
  scoped_ptr<SyncBackendHost> doomed_backend(backend_.release());

  if (doomed_backend.get())
    doomed_backend->Shutdown(sync_disabled);

  doomed_backend.reset();


  // Clear various flags.
  is_auth_in_progress_ = false;
  backend_initialized_ = false;
  expecting_first_run_auth_needed_event_ = false;
  last_attempted_user_email_.clear();
}

void ProfileSyncService::EnableForUser(gfx::NativeWindow parent_window) {
  if (WizardIsVisible()) {
    wizard_.Focus();
    return;
  }
  expecting_first_run_auth_needed_event_ = true;
  DCHECK(!data_type_manager_.get());

  wizard_.SetParent(parent_window);
  StartUp();
  FOR_EACH_OBSERVER(Observer, observers_, OnStateChanged());
}

void ProfileSyncService::DisableForUser() {
  LOG(INFO) << "Clearing Sync DB.";

  // Clear prefs (including  SyncSetupHasCompleted) before shutting down so
  // PSS clients don't think we're set up while we're shutting down.
  ClearPreferences();
  Shutdown(true);

  FOR_EACH_OBSERVER(Observer, observers_, OnStateChanged());
}

bool ProfileSyncService::HasSyncSetupCompleted() const {
  return profile_->GetPrefs()->GetBoolean(prefs::kSyncHasSetupCompleted);
}

void ProfileSyncService::SetSyncSetupCompleted() {
  PrefService* prefs = profile()->GetPrefs();
  prefs->SetBoolean(prefs::kSyncHasSetupCompleted, true);
  prefs->ScheduleSavePersistentPrefs();
}

void ProfileSyncService::UpdateLastSyncedTime() {
  last_synced_time_ = base::Time::Now();
  profile_->GetPrefs()->SetInt64(prefs::kSyncLastSyncedTime,
      last_synced_time_.ToInternalValue());
  profile_->GetPrefs()->ScheduleSavePersistentPrefs();
}

// static
const wchar_t* ProfileSyncService::GetPrefNameForDataType(
    syncable::ModelType data_type) {
  switch (data_type) {
    case syncable::BOOKMARKS:
      return prefs::kSyncBookmarks;
    case syncable::PASSWORDS:
      return prefs::kSyncPasswords;
    case syncable::PREFERENCES:
      return prefs::kSyncPreferences;
    case syncable::AUTOFILL:
      return prefs::kSyncAutofill;
    case syncable::THEMES:
      return prefs::kSyncThemes;
    case syncable::TYPED_URLS:
      return prefs::kSyncTypedUrls;
    case syncable::EXTENSIONS:
      return prefs::kSyncExtensions;
    default:
      NOTREACHED();
      return NULL;
  }
}

// An invariant has been violated.  Transition to an error state where we try
// to do as little work as possible, to avoid further corruption or crashes.
void ProfileSyncService::OnUnrecoverableError(
    const tracked_objects::Location& from_here,
    const std::string& message) {
  unrecoverable_error_detected_ = true;
  unrecoverable_error_message_ = message;
  unrecoverable_error_location_.reset(
      new tracked_objects::Location(from_here.function_name(),
                                    from_here.file_name(),
                                    from_here.line_number()));

  // Shut all data types down.
  if (data_type_manager_.get())
    data_type_manager_->Stop();

  // Tell the wizard so it can inform the user only if it is already open.
  wizard_.Step(SyncSetupWizard::FATAL_ERROR);

  FOR_EACH_OBSERVER(Observer, observers_, OnStateChanged());
  LOG(ERROR) << "Unrecoverable error detected -- ProfileSyncService unusable.";
  std::string location;
  from_here.Write(true, true, &location);
  LOG(ERROR) << location;

  if (SetupInProgress()) {
    // We've hit an error in the middle of a startup process- shutdown all the
    // backend stuff, and then restart it, so we're in the same state as before.
    MessageLoop::current()->PostTask(FROM_HERE,
        scoped_runnable_method_factory_.NewRunnableMethod(
        &ProfileSyncService::Shutdown, true));
    MessageLoop::current()->PostTask(FROM_HERE,
        scoped_runnable_method_factory_.NewRunnableMethod(
        &ProfileSyncService::StartUp));
  }
}

void ProfileSyncService::OnBackendInitialized() {
  backend_initialized_ = true;

  // The very first time the backend initializes is effectively the first time
  // we can say we successfully "synced".  last_synced_time_ will only be null
  // in this case, because the pref wasn't restored on StartUp.
  if (last_synced_time_.is_null())
    UpdateLastSyncedTime();
  FOR_EACH_OBSERVER(Observer, observers_, OnStateChanged());

  if (bootstrap_sync_authentication_) {
    SetSyncSetupCompleted();
  }

  if (HasSyncSetupCompleted())
    ConfigureDataTypeManager();
}

void ProfileSyncService::OnSyncCycleCompleted() {
  UpdateLastSyncedTime();
  FOR_EACH_OBSERVER(Observer, observers_, OnStateChanged());
}

void ProfileSyncService::OnAuthError() {
  last_auth_error_ = backend_->GetAuthError();
  // Protect against the in-your-face dialogs that pop out of nowhere.
  // Require the user to click somewhere to run the setup wizard in the case
  // of a steady-state auth failure.
  if (WizardIsVisible() || expecting_first_run_auth_needed_event_) {
    wizard_.Step(AuthError::NONE == last_auth_error_.state() ?
        SyncSetupWizard::GAIA_SUCCESS : SyncSetupWizard::GAIA_LOGIN);
  }

  if (expecting_first_run_auth_needed_event_) {
    last_auth_error_ = AuthError::None();
    expecting_first_run_auth_needed_event_ = false;
  }

  if (!WizardIsVisible()) {
    auth_error_time_ == base::TimeTicks::Now();
  }

  if (!auth_start_time_.is_null()) {
    UMA_HISTOGRAM_TIMES("Sync.AuthorizationTimeInNetwork",
                    base::TimeTicks::Now() - auth_start_time_);
    auth_start_time_ = base::TimeTicks();
  }

  is_auth_in_progress_ = false;
  // Fan the notification out to interested UI-thread components.
  FOR_EACH_OBSERVER(Observer, observers_, OnStateChanged());
}

void ProfileSyncService::OnStopSyncingPermanently() {
  if (SetupInProgress()) {
    wizard_.Step(SyncSetupWizard::SETUP_ABORTED_BY_PENDING_CLEAR);
    expect_sync_configuration_aborted_ = true;
  }

  DisableForUser();
}

void ProfileSyncService::ShowLoginDialog(gfx::NativeWindow parent_window) {
  if (WizardIsVisible()) {
    wizard_.Focus();
    return;
  }

  if (!auth_error_time_.is_null()) {
    UMA_HISTOGRAM_LONG_TIMES("Sync.ReauthorizationTime",
                             base::TimeTicks::Now() - auth_error_time_);
    auth_error_time_ = base::TimeTicks();  // Reset auth_error_time_ to null.
  }

  if (last_auth_error_.state() != AuthError::NONE) {
    wizard_.SetParent(parent_window);
    wizard_.Step(SyncSetupWizard::GAIA_LOGIN);
  }
}

void ProfileSyncService::ShowChooseDataTypes(gfx::NativeWindow parent_window) {
  if (WizardIsVisible()) {
    wizard_.Focus();
    return;
  }
  wizard_.SetParent(parent_window);
  wizard_.Step(SyncSetupWizard::CHOOSE_DATA_TYPES);
}

SyncBackendHost::StatusSummary ProfileSyncService::QuerySyncStatusSummary() {
  if (backend_.get())
    return backend_->GetStatusSummary();
  else
    return SyncBackendHost::Status::OFFLINE_UNUSABLE;
}

SyncBackendHost::Status ProfileSyncService::QueryDetailedSyncStatus() {
  if (backend_.get()) {
    return backend_->GetDetailedStatus();
  } else {
    SyncBackendHost::Status status =
        { SyncBackendHost::Status::OFFLINE_UNUSABLE };
    return status;
  }
}

std::wstring ProfileSyncService::BuildSyncStatusSummaryText(
  const sync_api::SyncManager::Status::Summary& summary) {
  switch (summary) {
    case sync_api::SyncManager::Status::OFFLINE:
      return L"OFFLINE";
    case sync_api::SyncManager::Status::OFFLINE_UNSYNCED:
      return L"OFFLINE_UNSYNCED";
    case sync_api::SyncManager::Status::SYNCING:
      return L"SYNCING";
    case sync_api::SyncManager::Status::READY:
      return L"READY";
    case sync_api::SyncManager::Status::CONFLICT:
      return L"CONFLICT";
    case sync_api::SyncManager::Status::OFFLINE_UNUSABLE:
      return L"OFFLINE_UNUSABLE";
    case sync_api::SyncManager::Status::INVALID:  // fall through
    default:
      return L"UNKNOWN";
  }
}

std::wstring ProfileSyncService::GetLastSyncedTimeString() const {
  if (last_synced_time_.is_null())
    return l10n_util::GetString(IDS_SYNC_TIME_NEVER);

  base::TimeDelta last_synced = base::Time::Now() - last_synced_time_;

  if (last_synced < base::TimeDelta::FromMinutes(1))
    return l10n_util::GetString(IDS_SYNC_TIME_JUST_NOW);

  return TimeFormat::TimeElapsed(last_synced);
}

string16 ProfileSyncService::GetAuthenticatedUsername() const {
  if (backend_.get())
    return backend_->GetAuthenticatedUsername();
  else
    return string16();
}

void ProfileSyncService::OnUserSubmittedAuth(
    const std::string& username, const std::string& password,
    const std::string& captcha) {
  if (!backend_.get()) {
    NOTREACHED();
    return;
  }
  last_attempted_user_email_ = username;
  is_auth_in_progress_ = true;
  FOR_EACH_OBSERVER(Observer, observers_, OnStateChanged());

  auth_start_time_ = base::TimeTicks::Now();
  backend_->Authenticate(username, password, captcha);
}

void ProfileSyncService::OnUserChoseDatatypes(bool sync_everything,
    const syncable::ModelTypeSet& chosen_types) {
  if (!backend_.get()) {
    NOTREACHED();
    return;
  }
  profile_->GetPrefs()->SetBoolean(prefs::kKeepEverythingSynced,
      sync_everything);

  ChangePreferredDataTypes(chosen_types);
  profile_->GetPrefs()->ScheduleSavePersistentPrefs();
}

void ProfileSyncService::OnUserCancelledDialog() {
  if (!profile_->GetPrefs()->GetBoolean(prefs::kSyncHasSetupCompleted)) {
    // A sync dialog was aborted before authentication.
    // Rollback.
    DisableForUser();
  }
  wizard_.SetParent(NULL);

  // Though an auth could still be in progress, once the dialog is closed we
  // don't want the UI to stay stuck in the "waiting for authentication" state
  // as that could take forever.  We set this to false so the buttons to re-
  // login will appear until either a) the original request finishes and
  // succeeds, calling OnAuthError(NONE), or b) the user clicks the button,
  // and tries to re-authenticate. (b) is a little awkward as this second
  // request will get queued behind the first and could wind up "undoing" the
  // good if invalid creds were provided, but it's an edge case and the user
  // can of course get themselves out of it.
  is_auth_in_progress_ = false;
  FOR_EACH_OBSERVER(Observer, observers_, OnStateChanged());
}

void ProfileSyncService::ChangePreferredDataTypes(
    const syncable::ModelTypeSet& preferred_types) {

  // Filter out any datatypes which aren't registered, or for which
  // the preference can't be set.
  syncable::ModelTypeSet registered_types;
  GetRegisteredDataTypes(&registered_types);
  for (int i = 0; i < syncable::MODEL_TYPE_COUNT; ++i) {
    syncable::ModelType model_type = syncable::ModelTypeFromInt(i);
    if (!registered_types.count(model_type))
      continue;
    const wchar_t* pref_name = GetPrefNameForDataType(model_type);
    if (!pref_name)
      continue;
    profile_->GetPrefs()->SetBoolean(pref_name,
        preferred_types.count(model_type) != 0);
  }

  // If we haven't initialized yet, don't configure the DTM as it could cause
  // association to start before a Directory has even been created.
  if (backend_initialized_)
    ConfigureDataTypeManager();
}

void ProfileSyncService::GetPreferredDataTypes(
    syncable::ModelTypeSet* preferred_types) const {
  preferred_types->clear();

  // Filter out any datatypes which aren't registered, or for which
  // the preference can't be read.
  syncable::ModelTypeSet registered_types;
  GetRegisteredDataTypes(&registered_types);
  for (int i = 0; i < syncable::MODEL_TYPE_COUNT; ++i) {
    syncable::ModelType model_type = syncable::ModelTypeFromInt(i);
    if (!registered_types.count(model_type))
      continue;
    const wchar_t* pref_name = GetPrefNameForDataType(model_type);
    if (!pref_name)
      continue;
    if (profile_->GetPrefs()->GetBoolean(pref_name))
      preferred_types->insert(model_type);
  }
}

void ProfileSyncService::GetRegisteredDataTypes(
    syncable::ModelTypeSet* registered_types) const {
  registered_types->clear();
  // The data_type_controllers_ are determined by command-line flags; that's
  // effectively what controls the values returned here.
  for (DataTypeController::TypeMap::const_iterator it =
       data_type_controllers_.begin();
       it != data_type_controllers_.end(); ++it) {
    registered_types->insert((*it).first);
  }
}

bool ProfileSyncService::IsCryptographerReady() const {
  return backend_->GetUserShareHandle()->
      dir_manager->cryptographer()->is_ready();
}

void ProfileSyncService::SetPassphrase(const std::string& passphrase) {
  backend_->SetPassphrase(passphrase);
}

void ProfileSyncService::ConfigureDataTypeManager() {
  if (!data_type_manager_.get()) {
    data_type_manager_.reset(
        factory_->CreateDataTypeManager(backend_.get(),
                                        data_type_controllers_));
  }

  syncable::ModelTypeSet types;
  GetPreferredDataTypes(&types);
  data_type_manager_->Configure(types);
}

void ProfileSyncService::ActivateDataType(
    DataTypeController* data_type_controller,
    ChangeProcessor* change_processor) {
  if (!backend_.get()) {
    NOTREACHED();
    return;
  }
  change_processor->Start(profile(), backend_->GetUserShareHandle());
  backend_->ActivateDataType(data_type_controller, change_processor);
}

void ProfileSyncService::DeactivateDataType(
    DataTypeController* data_type_controller,
    ChangeProcessor* change_processor) {
  change_processor->Stop();
  if (backend_.get())
    backend_->DeactivateDataType(data_type_controller, change_processor);
}

void ProfileSyncService::Observe(NotificationType type,
                                 const NotificationSource& source,
                                 const NotificationDetails& details) {
  switch (type.value) {
    case NotificationType::SYNC_CONFIGURE_START: {
      FOR_EACH_OBSERVER(Observer, observers_, OnStateChanged());
      // TODO(sync): Maybe toast?
      break;
    }
    case NotificationType::SYNC_CONFIGURE_DONE: {
      DataTypeManager::ConfigureResult result =
          *(Details<DataTypeManager::ConfigureResult>(details).ptr());
      if (result == DataTypeManager::ABORTED &&
          expect_sync_configuration_aborted_) {
        expect_sync_configuration_aborted_ = false;
        return;
      }
      if (result != DataTypeManager::OK) {
        OnUnrecoverableError(FROM_HERE, "Sync Configuration failed.");
        return;
      }

      // TODO(sync): Less wizard, more toast.
      wizard_.Step(SyncSetupWizard::DONE);
      FOR_EACH_OBSERVER(Observer, observers_, OnStateChanged());

      break;
    }
    case NotificationType::SYNC_PASSPHRASE_REQUIRED: {
      // TODO(sync): Show the passphrase UI here.
      SetPassphrase("dummy passphrase");
      break;
    }
    case NotificationType::SYNC_PASSPHRASE_ACCEPTED: {
      // Make sure the data types that depend on the passphrase are started at
      // this time.
      syncable::ModelTypeSet types;
      GetPreferredDataTypes(&types);
      data_type_manager_->Configure(types);

      FOR_EACH_OBSERVER(Observer, observers_, OnStateChanged());
      break;
    }
    case NotificationType::PREF_CHANGED: {
      std::wstring* pref_name = Details<std::wstring>(details).ptr();
      if (*pref_name == prefs::kSyncManaged) {
        FOR_EACH_OBSERVER(Observer, observers_, OnStateChanged());
        if (*pref_sync_managed_)
          DisableForUser();
        else if (HasSyncSetupCompleted())
          StartUp();
      }
      break;
    }
    default: {
      NOTREACHED();
    }
  }
}

void ProfileSyncService::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void ProfileSyncService::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void ProfileSyncService::SyncEvent(SyncEventCodes code) {
  UMA_HISTOGRAM_ENUMERATION("Sync.EventCodes", code, MAX_SYNC_EVENT_CODE);
}

// static
bool ProfileSyncService::IsSyncEnabled() {
  // We have switches::kEnableSync just in case we need to change back to
  // sync-disabled-by-default on a platform.
  return !CommandLine::ForCurrentProcess()->HasSwitch(switches::kDisableSync);
}

bool ProfileSyncService::IsManaged() {
  // Some tests use ProfileSyncServiceMock which doesn't have a profile.
  return profile_ && profile_->GetPrefs()->GetBoolean(prefs::kSyncManaged);
}

bool ProfileSyncService::ShouldPushChanges() {
  // True only after all bootstrapping has succeeded: the sync backend
  // is initialized, all enabled data types are consistent with one
  // another, and no unrecoverable error has transpired.
  if (unrecoverable_error_detected_)
    return false;

  if (!data_type_manager_.get())
    return false;

  return data_type_manager_->state() == DataTypeManager::CONFIGURED;
}
