// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_processes_api.h"

#include "base/values.h"

#include "chrome/browser/extensions/extension_tabs_module.h"
#include "chrome/browser/extensions/extension_processes_api_constants.h"
#include "chrome/browser/renderer_host/render_process_host.h"
#include "chrome/browser/tab_contents/tab_contents.h"

namespace keys = extension_processes_api_constants;

DictionaryValue* CreateProcessValue(int process_id) {
  DictionaryValue* result = new DictionaryValue();
  result->SetInteger(keys::kIdKey, process_id);

  return result;
}

bool GetProcessForTabFunction::RunImpl() {
  int tab_id;
  EXTENSION_FUNCTION_VALIDATE(args_->GetInteger(0, &tab_id));

  TabContents* contents = NULL;
  int tab_index = -1;
  if (!ExtensionTabUtil::GetTabById(tab_id, profile(), include_incognito(),
                                    NULL, NULL, &contents, &tab_index))
    return false;

  int process_id = contents->GetRenderProcessHost()->id();
  result_.reset(CreateProcessValue(process_id));
  return true;
}
