// Copyright (c) 2006-2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_GLUE_PLUGINS_TEST_PLUGIN_THREAD_ASYNC_CALL_TEST_H
#define WEBKIT_GLUE_PLUGINS_TEST_PLUGIN_THREAD_ASYNC_CALL_TEST_H

#include <vector>

#include "base/at_exit.h"
#include "base/scoped_ptr.h"
#include "webkit/glue/plugins/test/plugin_test.h"

namespace NPAPIClient {

// This class tests scheduling and unscheduling of async callbacks using
// NPN_PluginThreadAsyncCall.
class PluginThreadAsyncCallTest : public PluginTest {
 public:
  PluginThreadAsyncCallTest(NPP id, NPNetscapeFuncs *host_functions);

  virtual NPError New(uint16 mode, int16 argc, const char* argn[],
                      const char* argv[], NPSavedData* saved);

  virtual NPError Destroy();

  void AsyncCall();
  void OnCallSucceeded();
  void OnCallCompleted();

 private:
  // base::Thread needs one of these.
  scoped_ptr<base::AtExitManager> at_exit_manager_;
};

}  // namespace NPAPIClient

#endif  // WEBKIT_GLUE_PLUGINS_TEST_PLUGIN_THREAD_ASYNC_CALL_TEST_H
