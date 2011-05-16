// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DOM_UI_GPU_INTERNALS_UI_H_
#define CHROME_BROWSER_DOM_UI_GPU_INTERNALS_UI_H_
#pragma once

#include "chrome/browser/dom_ui/dom_ui.h"

class GpuInternalsUI : public DOMUI {
 public:
  explicit GpuInternalsUI(TabContents* contents);

 private:
  DISALLOW_COPY_AND_ASSIGN(GpuInternalsUI);
};

#endif  // CHROME_BROWSER_DOM_UI_GPU_INTERNALS_UI_H_

