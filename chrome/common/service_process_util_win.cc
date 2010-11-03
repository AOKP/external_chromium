// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/service_process_util.h"

#include "base/command_line.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/object_watcher.h"
#include "base/path_service.h"
#include "base/scoped_handle_win.h"
#include "base/string16.h"
#include "base/utf_string_conversions.h"
#include "base/win_util.h"
#include "chrome/common/chrome_switches.h"

namespace {

string16 GetServiceProcessReadyEventName() {
  return UTF8ToWide(
      GetServiceProcessScopedVersionedName("_service_ready"));
}

string16 GetServiceProcessShutdownEventName() {
  return UTF8ToWide(
      GetServiceProcessScopedVersionedName("_service_shutdown_evt"));
}

class ServiceProcessShutdownMonitor : public base::ObjectWatcher::Delegate {
 public:
  explicit ServiceProcessShutdownMonitor(Task* shutdown_task)
      : shutdown_task_(shutdown_task) {
  }
  void Start() {
    string16 event_name = GetServiceProcessShutdownEventName();
    CHECK(event_name.length() <= MAX_PATH);
    shutdown_event_.Set(CreateEvent(NULL, TRUE, FALSE, event_name.c_str()));
    watcher_.StartWatching(shutdown_event_.Get(), this);
  }

  // base::ObjectWatcher::Delegate implementation.
  virtual void OnObjectSignaled(HANDLE object) {
    shutdown_task_->Run();
    shutdown_task_.reset();
  }

 private:
  ScopedHandle shutdown_event_;
  base::ObjectWatcher watcher_;
  scoped_ptr<Task> shutdown_task_;
};

}  // namespace

bool ForceServiceProcessShutdown(const std::string& version) {
  ScopedHandle shutdown_event;
  std::string versioned_name = version;
  versioned_name.append("_service_shutdown_evt");
  string16 event_name =
      UTF8ToWide(GetServiceProcessScopedName(versioned_name));
  shutdown_event.Set(OpenEvent(EVENT_MODIFY_STATE, FALSE, event_name.c_str()));
  if (!shutdown_event.IsValid())
    return false;
  SetEvent(shutdown_event.Get());
  return true;
}

bool CheckServiceProcessReady() {
  string16 event_name = GetServiceProcessReadyEventName();
  ScopedHandle event(
      OpenEvent(SYNCHRONIZE | READ_CONTROL, false, event_name.c_str()));
  if (!event.IsValid())
    return false;
  // Check if the event is signaled.
  return WaitForSingleObject(event, 0) == WAIT_OBJECT_0;
}

struct ServiceProcessState::StateData {
  // An event that is signaled when a service process is ready.
  ScopedHandle ready_event;
  scoped_ptr<ServiceProcessShutdownMonitor> shutdown_monitor;
};

bool ServiceProcessState::TakeSingletonLock() {
  DCHECK(!state_);
  string16 event_name = GetServiceProcessReadyEventName();
  CHECK(event_name.length() <= MAX_PATH);
  ScopedHandle service_process_ready_event;
  service_process_ready_event.Set(
      CreateEvent(NULL, TRUE, FALSE, event_name.c_str()));
  DWORD error = GetLastError();
  if ((error == ERROR_ALREADY_EXISTS) || (error == ERROR_ACCESS_DENIED))
    return false;
  DCHECK(service_process_ready_event.IsValid());
  state_ = new StateData;
  state_->ready_event.Set(service_process_ready_event.Take());
  return true;
}

void ServiceProcessState::SignalReady(Task* shutdown_task) {
  DCHECK(state_);
  DCHECK(state_->ready_event.IsValid());
  SetEvent(state_->ready_event.Get());
  if (shutdown_task) {
    state_->shutdown_monitor.reset(
        new ServiceProcessShutdownMonitor(shutdown_task));
    state_->shutdown_monitor->Start();
  }
}

void ServiceProcessState::SignalStopped() {
  TearDownState();
  shared_mem_service_data_.reset();
}

bool ServiceProcessState::AddToAutoRun() {
  FilePath chrome_path;
  if (PathService::Get(base::FILE_EXE, &chrome_path)) {
    CommandLine cmd_line(chrome_path);
    cmd_line.AppendSwitchASCII(switches::kProcessType,
                               switches::kServiceProcess);
    // We need a unique name for the command per user-date-dir. Just use the
    // channel name.
    return win_util::AddCommandToAutoRun(
        HKEY_CURRENT_USER,
        UTF8ToWide(GetAutoRunKey()),
        cmd_line.command_line_string());
  }
  return false;
}

bool ServiceProcessState::RemoveFromAutoRun() {
  return win_util::RemoveCommandFromAutoRun(
      HKEY_CURRENT_USER, UTF8ToWide(GetAutoRunKey()));
}

void ServiceProcessState::TearDownState() {
  delete state_;
  state_ = NULL;
}

bool ServiceProcessState::ShouldHandleOtherVersion() {
  return true;
}
