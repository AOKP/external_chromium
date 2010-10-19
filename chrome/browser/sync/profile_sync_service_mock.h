// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_PROFILE_SYNC_SERVICE_MOCK_H_
#define CHROME_BROWSER_SYNC_PROFILE_SYNC_SERVICE_MOCK_H_
#pragma once

#include <string>
#include "base/string16.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/sync/glue/change_processor.h"
#include "chrome/browser/sync/glue/data_type_controller.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/sync/syncable/model_type.h"
#include "testing/gmock/include/gmock/gmock.h"

class ProfileSyncServiceMock : public ProfileSyncService {
 public:
  ProfileSyncServiceMock() {}
  virtual ~ProfileSyncServiceMock() {}

  MOCK_METHOD0(DisableForUser, void());
  MOCK_METHOD0(OnBackendInitialized, void());
  MOCK_METHOD0(OnSyncCycleCompleted, void());
  MOCK_METHOD0(OnAuthError, void());
  MOCK_METHOD3(OnUserSubmittedAuth,
               void(const std::string& username,
                    const std::string& password,
                    const std::string& captcha));
  MOCK_METHOD0(OnUserCancelledDialog, void());
  MOCK_CONST_METHOD0(GetAuthenticatedUsername, string16());
  MOCK_METHOD2(OnUnrecoverableError,
               void(const tracked_objects::Location& location,
               const std::string& message));
  MOCK_METHOD2(ActivateDataType,
               void(browser_sync::DataTypeController* data_type_controller,
                    browser_sync::ChangeProcessor* change_processor));
  MOCK_METHOD2(DeactivateDataType,
               void(browser_sync::DataTypeController* data_type_controller,
                    browser_sync::ChangeProcessor* change_processor));

  MOCK_METHOD0(InitializeBackend, void());
  MOCK_METHOD1(AddObserver, void(Observer*));
  MOCK_METHOD1(RemoveObserver, void(Observer*));
  MOCK_CONST_METHOD0(HasSyncSetupCompleted, bool());

  MOCK_METHOD1(ChangePreferredDataTypes,
               void(const syncable::ModelTypeSet& preferred_types));
  MOCK_CONST_METHOD1(GetPreferredDataTypes,
                     void(syncable::ModelTypeSet* preferred_types));
  MOCK_CONST_METHOD1(GetRegisteredDataTypes,
                     void(syncable::ModelTypeSet* registered_types));
};

#endif  // CHROME_BROWSER_SYNC_PROFILE_SYNC_SERVICE_MOCK_H_
