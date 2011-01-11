// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_PRINTING_CLOUD_PRINT_CLOUD_PRINT_SETUP_MESSAGE_HANDLER_H_
#define CHROME_BROWSER_PRINTING_CLOUD_PRINT_CLOUD_PRINT_SETUP_MESSAGE_HANDLER_H_

#include <string>

#include "base/values.h"
#include "chrome/browser/dom_ui/html_dialog_ui.h"

class CloudPrintSetupFlow;

// This class is used to handle DOM messages from the setup dialog.
class CloudPrintSetupMessageHandler : public DOMMessageHandler {
 public:
  explicit CloudPrintSetupMessageHandler(CloudPrintSetupFlow* flow)
    : flow_(flow) {}
  virtual ~CloudPrintSetupMessageHandler() {}

  // DOMMessageHandler implementation.
  virtual DOMMessageHandler* Attach(DOMUI* dom_ui);

 protected:
  // DOMMessageHandler implementation.
  virtual void RegisterMessages();

  // Callbacks from the page.
  void HandleSubmitAuth(const ListValue* args);
  void HandlePrintTestPage(const ListValue* args);
  void HandleLearnMore(const ListValue* args);

 private:
  CloudPrintSetupFlow* flow_;

  DISALLOW_COPY_AND_ASSIGN(CloudPrintSetupMessageHandler);
};

#endif  // CHROME_BROWSER_PRINTING_CLOUD_PRINT_CLOUD_PRINT_SETUP_MESSAGE_HANDLER_H_
