// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/cocoa/about_ipc_dialog.h"
#include "chrome/browser/ui/cocoa/about_ipc_controller.h"

#if defined(IPC_MESSAGE_LOG_ENABLED)

namespace AboutIPCDialog {

void RunDialog() {
  // The controller gets deallocated when then window is closed,
  // so it is safe to "fire and forget".
  AboutIPCController* controller = [AboutIPCController sharedController];
  [[controller window] makeKeyAndOrderFront:controller];
}

};  // namespace AboutIPCDialog

#endif  // IPC_MESSAGE_LOG_ENABLED
