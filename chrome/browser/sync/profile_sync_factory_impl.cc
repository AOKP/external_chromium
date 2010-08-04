// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "base/logging.h"
#include "chrome/browser/defaults.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/sync/glue/autofill_change_processor.h"
#include "chrome/browser/sync/glue/autofill_data_type_controller.h"
#include "chrome/browser/sync/glue/autofill_model_associator.h"
#include "chrome/browser/sync/glue/bookmark_change_processor.h"
#include "chrome/browser/sync/glue/bookmark_data_type_controller.h"
#include "chrome/browser/sync/glue/bookmark_model_associator.h"
#include "chrome/browser/sync/glue/data_type_manager_impl.h"
#include "chrome/browser/sync/glue/extension_change_processor.h"
#include "chrome/browser/sync/glue/extension_data_type_controller.h"
#include "chrome/browser/sync/glue/extension_model_associator.h"
#include "chrome/browser/sync/glue/password_change_processor.h"
#include "chrome/browser/sync/glue/password_data_type_controller.h"
#include "chrome/browser/sync/glue/password_model_associator.h"
#include "chrome/browser/sync/glue/preference_change_processor.h"
#include "chrome/browser/sync/glue/preference_data_type_controller.h"
#include "chrome/browser/sync/glue/preference_model_associator.h"
#include "chrome/browser/sync/glue/sync_backend_host.h"
#include "chrome/browser/sync/glue/theme_change_processor.h"
#include "chrome/browser/sync/glue/theme_data_type_controller.h"
#include "chrome/browser/sync/glue/theme_model_associator.h"
#include "chrome/browser/sync/glue/typed_url_change_processor.h"
#include "chrome/browser/sync/glue/typed_url_data_type_controller.h"
#include "chrome/browser/sync/glue/typed_url_model_associator.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/sync/profile_sync_factory_impl.h"
#include "chrome/browser/webdata/web_data_service.h"
#include "chrome/common/chrome_switches.h"

using browser_sync::AutofillChangeProcessor;
using browser_sync::AutofillDataTypeController;
using browser_sync::AutofillModelAssociator;
using browser_sync::BookmarkChangeProcessor;
using browser_sync::BookmarkDataTypeController;
using browser_sync::BookmarkModelAssociator;
using browser_sync::DataTypeController;
using browser_sync::DataTypeManager;
using browser_sync::DataTypeManagerImpl;
using browser_sync::ExtensionChangeProcessor;
using browser_sync::ExtensionDataTypeController;
using browser_sync::ExtensionModelAssociator;
using browser_sync::PasswordChangeProcessor;
using browser_sync::PasswordDataTypeController;
using browser_sync::PasswordModelAssociator;
using browser_sync::PreferenceChangeProcessor;
using browser_sync::PreferenceDataTypeController;
using browser_sync::PreferenceModelAssociator;
using browser_sync::SyncBackendHost;
using browser_sync::ThemeChangeProcessor;
using browser_sync::ThemeDataTypeController;
using browser_sync::ThemeModelAssociator;
using browser_sync::TypedUrlChangeProcessor;
using browser_sync::TypedUrlDataTypeController;
using browser_sync::TypedUrlModelAssociator;
using browser_sync::UnrecoverableErrorHandler;

ProfileSyncFactoryImpl::ProfileSyncFactoryImpl(Profile* profile,
                                               CommandLine* command_line)
    : profile_(profile),
      command_line_(command_line) {
}

ProfileSyncService* ProfileSyncFactoryImpl::CreateProfileSyncService() {
  ProfileSyncService* pss = new ProfileSyncService(
      this, profile_, browser_defaults::kBootstrapSyncAuthentication);

  // Autofill sync is enabled by default.  Register unless explicitly
  // disabled.
  if (!command_line_->HasSwitch(switches::kDisableSyncAutofill)) {
    pss->RegisterDataTypeController(
        new AutofillDataTypeController(this, profile_, pss));
  }

  // Bookmark sync is enabled by default.  Register unless explicitly
  // disabled.
  if (!command_line_->HasSwitch(switches::kDisableSyncBookmarks)) {
    pss->RegisterDataTypeController(
        new BookmarkDataTypeController(this, profile_, pss));
  }

  // Extension sync is enabled by default.  Register unless explicitly
  // disabled.
  if (!command_line_->HasSwitch(switches::kDisableSyncExtensions)) {
    pss->RegisterDataTypeController(
        new ExtensionDataTypeController(this, profile_, pss));
  }

  // Password sync is disabled by default.  Register only if
  // explicitly enabled.
  if (command_line_->HasSwitch(switches::kEnableSyncPasswords)) {
    pss->RegisterDataTypeController(
        new PasswordDataTypeController(this, profile_, pss));
  }

  // Preference sync is enabled by default.  Register unless explicitly
  // disabled.
  if (!command_line_->HasSwitch(switches::kDisableSyncPreferences)) {
    pss->RegisterDataTypeController(
        new PreferenceDataTypeController(this, pss));
  }

  // Theme sync is enabled by default.  Register unless explicitly disabled.
  if (!command_line_->HasSwitch(switches::kDisableSyncThemes)) {
    pss->RegisterDataTypeController(
        new ThemeDataTypeController(this, profile_, pss));
  }

  // TypedUrl sync is disabled by default.  Register only if
  // explicitly enabled.
  if (command_line_->HasSwitch(switches::kEnableSyncTypedUrls)) {
    pss->RegisterDataTypeController(
        new TypedUrlDataTypeController(this, profile_, pss));
  }

  return pss;
}

DataTypeManager* ProfileSyncFactoryImpl::CreateDataTypeManager(
    SyncBackendHost* backend,
    const DataTypeController::TypeMap& controllers) {
  return new DataTypeManagerImpl(backend, controllers);
}

ProfileSyncFactory::SyncComponents
ProfileSyncFactoryImpl::CreateAutofillSyncComponents(
    ProfileSyncService* profile_sync_service,
    WebDatabase* web_database,
    PersonalDataManager* personal_data,
    browser_sync::UnrecoverableErrorHandler* error_handler) {
  AutofillModelAssociator* model_associator =
      new AutofillModelAssociator(profile_sync_service,
                                  web_database,
                                  personal_data);
  AutofillChangeProcessor* change_processor =
      new AutofillChangeProcessor(model_associator,
                                  web_database,
                                  personal_data,
                                  error_handler);
  return SyncComponents(model_associator, change_processor);
}

ProfileSyncFactory::SyncComponents
ProfileSyncFactoryImpl::CreateBookmarkSyncComponents(
    ProfileSyncService* profile_sync_service,
    UnrecoverableErrorHandler* error_handler) {
  BookmarkModelAssociator* model_associator =
      new BookmarkModelAssociator(profile_sync_service,
                                  error_handler);
  BookmarkChangeProcessor* change_processor =
      new BookmarkChangeProcessor(model_associator,
                                  error_handler);
  return SyncComponents(model_associator, change_processor);
}

ProfileSyncFactory::SyncComponents
ProfileSyncFactoryImpl::CreateExtensionSyncComponents(
    ProfileSyncService* profile_sync_service,
    UnrecoverableErrorHandler* error_handler) {
  ExtensionModelAssociator* model_associator =
      new ExtensionModelAssociator(profile_sync_service);
  ExtensionChangeProcessor* change_processor =
      new ExtensionChangeProcessor(error_handler, model_associator);
  return SyncComponents(model_associator, change_processor);
}

ProfileSyncFactory::SyncComponents
ProfileSyncFactoryImpl::CreatePasswordSyncComponents(
    ProfileSyncService* profile_sync_service,
    PasswordStore* password_store,
    UnrecoverableErrorHandler* error_handler) {
  PasswordModelAssociator* model_associator =
      new PasswordModelAssociator(profile_sync_service,
                                  password_store);
  PasswordChangeProcessor* change_processor =
      new PasswordChangeProcessor(model_associator,
                                  password_store,
                                  error_handler);
  return SyncComponents(model_associator, change_processor);
}

ProfileSyncFactory::SyncComponents
ProfileSyncFactoryImpl::CreatePreferenceSyncComponents(
    ProfileSyncService* profile_sync_service,
    UnrecoverableErrorHandler* error_handler) {
  PreferenceModelAssociator* model_associator =
      new PreferenceModelAssociator(profile_sync_service);
  PreferenceChangeProcessor* change_processor =
      new PreferenceChangeProcessor(model_associator,
                                    error_handler);
  return SyncComponents(model_associator, change_processor);
}

ProfileSyncFactory::SyncComponents
ProfileSyncFactoryImpl::CreateThemeSyncComponents(
    ProfileSyncService* profile_sync_service,
    UnrecoverableErrorHandler* error_handler) {
  ThemeModelAssociator* model_associator =
      new ThemeModelAssociator(profile_sync_service);
  ThemeChangeProcessor* change_processor =
      new ThemeChangeProcessor(error_handler);
  return SyncComponents(model_associator, change_processor);
}

ProfileSyncFactory::SyncComponents
ProfileSyncFactoryImpl::CreateTypedUrlSyncComponents(
    ProfileSyncService* profile_sync_service,
    history::HistoryBackend* history_backend,
    browser_sync::UnrecoverableErrorHandler* error_handler) {
  TypedUrlModelAssociator* model_associator =
      new TypedUrlModelAssociator(profile_sync_service,
                                  history_backend);
  TypedUrlChangeProcessor* change_processor =
      new TypedUrlChangeProcessor(model_associator,
                                  history_backend,
                                  error_handler);
  return SyncComponents(model_associator, change_processor);
}
