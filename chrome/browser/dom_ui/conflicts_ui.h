// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DOM_UI_CONFLICTS_UI_H_
#define CHROME_BROWSER_DOM_UI_CONFLICTS_UI_H_
#pragma once

#include "chrome/browser/dom_ui/dom_ui.h"

#if defined(OS_WIN)

class RefCountedMemory;

// The DOM UI handler for about:conflicts.
class ConflictsUI : public DOMUI {
 public:
  explicit ConflictsUI(TabContents* contents);

  static RefCountedMemory* GetFaviconResourceBytes();

 private:
  DISALLOW_COPY_AND_ASSIGN(ConflictsUI);
};

#endif

#endif  // CHROME_BROWSER_DOM_UI_CONFLICTS_UI_H_
