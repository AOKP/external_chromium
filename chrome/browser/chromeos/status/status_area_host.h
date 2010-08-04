// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_STATUS_STATUS_AREA_HOST_H_
#define CHROME_BROWSER_CHROMEOS_STATUS_STATUS_AREA_HOST_H_

#include "gfx/native_widget_types.h"

namespace views {
class View;
}  // namespace views

class Profile;

namespace chromeos {

// This class is an abstraction decoupling StatusAreaView from its host
// window.
class StatusAreaHost {
 public:
  // Returns the Profile if this status area is inside the browser and has a
  // profile. Otherwise, returns NULL.
  virtual Profile* GetProfile() const = 0;

  // Returns native window hosting the status area.
  virtual gfx::NativeWindow GetNativeWindow() const = 0;

  // Indicates if options dialog related to the button specified should be
  // shown.
  virtual bool ShouldOpenButtonOptions(
      const views::View* button_view) const = 0;

  // Opens options dialog related to the button specified.
  virtual void OpenButtonOptions(const views::View* button_view) const = 0;

  // Executes browser command.
  virtual void ExecuteBrowserCommand(int id) const = 0;

  // True if status area hosted in browser. Otherwise it's OOBE/login state.
  virtual bool IsBrowserMode() const = 0;

  // True if status area hosted in screen locker.
  virtual bool IsScreenLockerMode() const = 0;

 protected:
  virtual ~StatusAreaHost() {}
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_STATUS_STATUS_AREA_HOST_H_
