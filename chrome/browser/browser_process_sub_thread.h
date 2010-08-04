// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_BROWSER_PROCESS_SUB_THREAD_H_
#define CHROME_BROWSER_BROWSER_PROCESS_SUB_THREAD_H_

#include "base/basictypes.h"
#include "chrome/browser/chrome_thread.h"

class NotificationService;

// ----------------------------------------------------------------------------
// BrowserProcessSubThread
//
// This simple thread object is used for the specialized threads that the
// BrowserProcess spins up.
//
// Applications must initialize the COM library before they can call
// COM library functions other than CoGetMalloc and memory allocation
// functions, so this class initializes COM for those users.
class BrowserProcessSubThread : public ChromeThread {
 public:
  explicit BrowserProcessSubThread(ChromeThread::ID identifier);
  virtual ~BrowserProcessSubThread();

 protected:
  virtual void Init();
  virtual void CleanUp();

 private:
  // Each specialized thread has its own notification service.
  // Note: We don't use scoped_ptr because the destructor runs on the wrong
  // thread.
  NotificationService* notification_service_;

  DISALLOW_COPY_AND_ASSIGN(BrowserProcessSubThread);
};

#endif // CHROME_BROWSER_BROWSER_PROCESS_SUB_THREAD_H_
