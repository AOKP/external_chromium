// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// C++ bridge function to connect ExtensionInstallUI to the Cocoa-based
// extension installed bubble.

#ifndef CHROME_BROWSER_COCOA_EXTENSION_INSTALLED_BUBBLE_BRIDGE_H_
#define CHROME_BROWSER_COCOA_EXTENSION_INSTALLED_BUBBLE_BRIDGE_H_
#pragma once

#include "gfx/native_widget_types.h"
#include "third_party/skia/include/core/SkBitmap.h"

class Browser;
class Extension;

namespace ExtensionInstalledBubbleCocoa {

// This function is called by the ExtensionInstallUI when an extension has been
// installed.
void ShowExtensionInstalledBubble(gfx::NativeWindow window,
                                  Extension* extension,
                                  Browser* browser,
                                  SkBitmap icon);
}

#endif  // CHROME_BROWSER_COCOA_EXTENSION_INSTALLED_BUBBLE_BRIDGE_H_
