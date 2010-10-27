// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DOM_UI_STOP_SYNCING_HANDLER_H_
#define CHROME_BROWSER_DOM_UI_STOP_SYNCING_HANDLER_H_

#include "chrome/browser/dom_ui/options_ui.h"

// Chrome personal stuff stop syncing overlay UI handler.
class StopSyncingHandler : public OptionsPageUIHandler {
 public:
  StopSyncingHandler();
  virtual ~StopSyncingHandler();

  // OptionsUIHandler implementation.
  virtual void GetLocalizedValues(DictionaryValue* localized_strings);

  // DOMMessageHandler implementation.
  virtual void RegisterMessages();

 private:
  void StopSyncing(const ListValue* args);

  DISALLOW_COPY_AND_ASSIGN(StopSyncingHandler);
};

#endif  // CHROME_BROWSER_DOM_UI_STOP_SYNCING_HANDLER_H_
