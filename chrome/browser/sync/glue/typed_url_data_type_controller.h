// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_GLUE_TYPED_URL_DATA_TYPE_CONTROLLER_H__
#define CHROME_BROWSER_SYNC_GLUE_TYPED_URL_DATA_TYPE_CONTROLLER_H__
#pragma once

#include <string>

#include "base/basictypes.h"
#include "base/scoped_ptr.h"
#include "chrome/browser/cancelable_request.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/sync/glue/data_type_controller.h"
#include "chrome/common/notification_observer.h"
#include "chrome/common/notification_registrar.h"
#include "chrome/common/notification_type.h"

class NotificationSource;
class NotificationDetails;
class Profile;
class ProfileSyncFactory;
class ProfileSyncService;

namespace browser_sync {

class AssociatorInterface;
class ChangeProcessor;
class ControlTask;

// A class that manages the startup and shutdown of typed_url sync.
class TypedUrlDataTypeController : public DataTypeController,
                                   public NotificationObserver,
                                   public CancelableRequestConsumerBase {
 public:
  TypedUrlDataTypeController(
      ProfileSyncFactory* profile_sync_factory,
      Profile* profile,
      ProfileSyncService* sync_service);
  virtual ~TypedUrlDataTypeController();

  // DataTypeController implementation
  virtual void Start(StartCallback* start_callback);

  virtual void Stop();

  virtual bool enabled() {
    return true;
  }

  virtual syncable::ModelType type() {
    return syncable::TYPED_URLS;
  }

  virtual browser_sync::ModelSafeGroup model_safe_group() {
    return browser_sync::GROUP_HISTORY;
  }

  virtual const char* name() const {
    // For logging only.
    return "typed_url";
  }

  virtual State state() {
    return state_;
  }

  // UnrecoverableHandler implementation
  virtual void OnUnrecoverableError(const tracked_objects::Location& from_here,
                                    const std::string& message);

  // NotificationObserver implementation.
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

  // CancelableRequestConsumerBase implementation.
  virtual void OnRequestAdded(CancelableRequestProvider* provider,
                              CancelableRequestProvider::Handle handle) {}

  virtual void OnRequestRemoved(CancelableRequestProvider* provider,
                                CancelableRequestProvider::Handle handle) {}

  virtual void WillExecute(CancelableRequestProvider* provider,
                           CancelableRequestProvider::Handle handle) {}

  virtual void DidExecute(CancelableRequestProvider* provider,
                          CancelableRequestProvider::Handle handle) {}

 private:
  friend class ControlTask;
  void StartImpl(history::HistoryBackend* backend);
  void StartDone(StartResult result, State state);
  void StartDoneImpl(StartResult result, State state);
  void StopImpl();
  void StartFailed(StartResult result);
  void OnUnrecoverableErrorImpl(const tracked_objects::Location& from_here,
                                const std::string& message);

  void set_state(State state) {
    DCHECK(ChromeThread::CurrentlyOn(ChromeThread::UI));
    state_ = state;
  }

  ProfileSyncFactory* profile_sync_factory_;
  Profile* profile_;
  ProfileSyncService* sync_service_;
  State state_;

  scoped_ptr<AssociatorInterface> model_associator_;
  scoped_ptr<ChangeProcessor> change_processor_;
  scoped_ptr<StartCallback> start_callback_;
  scoped_refptr<HistoryService> history_service_;

  NotificationRegistrar notification_registrar_;

  DISALLOW_COPY_AND_ASSIGN(TypedUrlDataTypeController);
};

}  // namespace browser_sync

#endif  // CHROME_BROWSER_SYNC_GLUE_TYPED_URL_DATA_TYPE_CONTROLLER_H__
