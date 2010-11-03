// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process_util.h"
#include "base/test/test_timeouts.h"
#include "chrome/browser/browser.h"
#include "chrome/browser/service/service_process_control.h"
#include "chrome/browser/service/service_process_control_manager.h"
#include "chrome/common/chrome_version_info.h"
#include "chrome/common/service_process_util.h"
#include "chrome/test/in_process_browser_test.h"
#include "chrome/test/ui_test_utils.h"

class ServiceProcessControlBrowserTest
    : public InProcessBrowserTest,
      public ServiceProcessControl::MessageHandler {
 public:
  ServiceProcessControlBrowserTest()
      : service_process_handle_(base::kNullProcessHandle) {
  }
  ~ServiceProcessControlBrowserTest() {
    base::CloseProcessHandle(service_process_handle_);
    service_process_handle_ = base::kNullProcessHandle;
    // Delete all instances of ServiceProcessControl.
    ServiceProcessControlManager::instance()->Shutdown();
  }

 protected:
  void LaunchServiceProcessControl() {
    ServiceProcessControl* process =
        ServiceProcessControlManager::instance()->GetProcessControl(
            browser()->profile());
    process_ = process;

    // Launch the process asynchronously.
    process->Launch(
        NewRunnableMethod(
            this,
            &ServiceProcessControlBrowserTest::ProcessControlLaunched),
        NewRunnableMethod(
            this,
            &ServiceProcessControlBrowserTest::ProcessControlLaunchFailed));

    // Then run the message loop to keep things running.
    ui_test_utils::RunMessageLoop();
  }

  void SayHelloAndWait() {
    // Send a hello message to the service process and wait for a reply.
    process()->SendHello();
    ui_test_utils::RunMessageLoop();
  }

  void Disconnect() {
    // This will delete all instances of ServiceProcessControl and close the IPC
    // connections.
    ServiceProcessControlManager::instance()->Shutdown();
    process_ = NULL;
  }

  void WaitForShutdown() {
    EXPECT_TRUE(base::WaitForSingleProcess(
        service_process_handle_,
        TestTimeouts::wait_for_terminate_timeout_ms()));
  }

  void ProcessControlLaunched() {
    base::ProcessId service_pid = GetServiceProcessPid();
    EXPECT_NE(static_cast<base::ProcessId>(0), service_pid);
    EXPECT_TRUE(base::OpenProcessHandleWithAccess(
        service_pid,
        base::kProcessAccessWaitForTermination,
        &service_process_handle_));
    process()->SetMessageHandler(this);
    // Quit the current message. Post a QuitTask instead of just calling Quit()
    // because this can get invoked in the context of a Launch() call and we
    // may not be in Run() yet.
    MessageLoop::current()->PostTask(FROM_HERE, new MessageLoop::QuitTask());
  }

  void ProcessControlLaunchFailed() {
    ADD_FAILURE();
    // Quit the current message.
    MessageLoop::current()->PostTask(FROM_HERE, new MessageLoop::QuitTask());
  }

  // ServiceProcessControl::MessageHandler implementations.
  virtual void OnGoodDay() {
    MessageLoop::current()->Quit();
  }

  ServiceProcessControl* process() { return process_; }

 private:
  ServiceProcessControl* process_;
  base::ProcessHandle service_process_handle_;
};

#if defined(OS_WIN)
// They way that the IPC is implemented only works on windows. This has to
// change when we implement a different scheme for IPC.
IN_PROC_BROWSER_TEST_F(ServiceProcessControlBrowserTest, LaunchAndIPC) {
  LaunchServiceProcessControl();

  // Make sure we are connected to the service process.
  EXPECT_TRUE(process()->is_connected());
  SayHelloAndWait();

  // And then shutdown the service process.
  EXPECT_TRUE(process()->Shutdown());
}

// This tests the case when a service process is launched when browser
// starts but we try to launch it again in the remoting setup dialog.
IN_PROC_BROWSER_TEST_F(ServiceProcessControlBrowserTest, LaunchTwice) {
  // Launch the service process the first time.
  LaunchServiceProcessControl();

  // Make sure we are connected to the service process.
  EXPECT_TRUE(process()->is_connected());
  SayHelloAndWait();

  // Launch the service process again.
  LaunchServiceProcessControl();
  EXPECT_TRUE(process()->is_connected());
  SayHelloAndWait();

  // And then shutdown the service process.
  EXPECT_TRUE(process()->Shutdown());
}

static void DecrementUntilZero(int* count) {
  (*count)--;
  if (!(*count))
    MessageLoop::current()->PostTask(FROM_HERE, new MessageLoop::QuitTask());
}

// Invoke multiple Launch calls in succession and ensure that all the tasks
// get invoked.
IN_PROC_BROWSER_TEST_F(ServiceProcessControlBrowserTest, MultipleLaunchTasks) {
  ServiceProcessControl* process =
      ServiceProcessControlManager::instance()->GetProcessControl(
          browser()->profile());
  int launch_count = 5;
  for (int i = 0; i < launch_count; i++) {
    // Launch the process asynchronously.
    process->Launch(
        NewRunnableFunction(&DecrementUntilZero, &launch_count),
        new MessageLoop::QuitTask());
  }
  // Then run the message loop to keep things running.
  ui_test_utils::RunMessageLoop();
  EXPECT_EQ(0, launch_count);
  // And then shutdown the service process.
  EXPECT_TRUE(process->Shutdown());
}

// Make sure using the same task for success and failure tasks works.
IN_PROC_BROWSER_TEST_F(ServiceProcessControlBrowserTest, SameLaunchTask) {
  ServiceProcessControl* process =
      ServiceProcessControlManager::instance()->GetProcessControl(
          browser()->profile());
  int launch_count = 5;
  for (int i = 0; i < launch_count; i++) {
    // Launch the process asynchronously.
    Task * task = NewRunnableFunction(&DecrementUntilZero, &launch_count);
    process->Launch(task, task);
  }
  // Then run the message loop to keep things running.
  ui_test_utils::RunMessageLoop();
  EXPECT_EQ(0, launch_count);
  // And then shutdown the service process.
  EXPECT_TRUE(process->Shutdown());
}

// Tests whether disconnecting from the service IPC causes the service process
// to die.
IN_PROC_BROWSER_TEST_F(ServiceProcessControlBrowserTest, DieOnDisconnect) {
  // Launch the service process.
  LaunchServiceProcessControl();
  // Make sure we are connected to the service process.
  EXPECT_TRUE(process()->is_connected());
  Disconnect();
  WaitForShutdown();
}

IN_PROC_BROWSER_TEST_F(ServiceProcessControlBrowserTest, ForceShutdown) {
  // Launch the service process.
  LaunchServiceProcessControl();
  // Make sure we are connected to the service process.
  EXPECT_TRUE(process()->is_connected());
  chrome::VersionInfo version_info;
  ForceServiceProcessShutdown(version_info.Version());
  WaitForShutdown();
}

IN_PROC_BROWSER_TEST_F(ServiceProcessControlBrowserTest, CheckPid) {
  EXPECT_EQ(0, GetServiceProcessPid());
  // Launch the service process.
  LaunchServiceProcessControl();
  EXPECT_NE(static_cast<base::ProcessId>(0), GetServiceProcessPid());
}

#endif

DISABLE_RUNNABLE_METHOD_REFCOUNT(ServiceProcessControlBrowserTest);
