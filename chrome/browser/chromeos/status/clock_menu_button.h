// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_STATUS_CLOCK_MENU_BUTTON_H_
#define CHROME_BROWSER_CHROMEOS_STATUS_CLOCK_MENU_BUTTON_H_
#pragma once

#include "base/scoped_ptr.h"
#include "base/timer.h"
#include "chrome/browser/chromeos/cros/system_library.h"
#include "chrome/browser/chromeos/status/status_area_button.h"
#include "chrome/common/notification_observer.h"
#include "chrome/common/notification_service.h"
#include "unicode/calendar.h"
#include "views/controls/button/menu_button.h"
#include "views/controls/menu/menu_2.h"
#include "views/controls/menu/view_menu_delegate.h"

namespace chromeos {

class StatusAreaHost;

// The clock menu button in the status area.
// This button shows the current time.
class ClockMenuButton : public StatusAreaButton,
                        public views::ViewMenuDelegate,
                        public menus::MenuModel,
                        public SystemLibrary::Observer {
 public:
  explicit ClockMenuButton(StatusAreaHost* host);
  virtual ~ClockMenuButton();

  // menus::MenuModel implementation.
  virtual bool HasIcons() const  { return false; }
  virtual int GetItemCount() const;
  virtual menus::MenuModel::ItemType GetTypeAt(int index) const;
  virtual int GetCommandIdAt(int index) const { return index; }
  virtual string16 GetLabelAt(int index) const;
  virtual bool IsLabelDynamicAt(int index) const { return true; }
  virtual bool GetAcceleratorAt(int index,
      menus::Accelerator* accelerator) const { return false; }
  virtual bool IsItemCheckedAt(int index) const { return false; }
  virtual int GetGroupIdAt(int index) const { return 0; }
  virtual bool GetIconAt(int index, SkBitmap* icon) const { return false; }
  virtual menus::ButtonMenuItemModel* GetButtonMenuItemAt(int index) const {
    return NULL;
  }
  virtual bool IsEnabledAt(int index) const;
  virtual menus::MenuModel* GetSubmenuModelAt(int index) const { return NULL; }
  virtual void HighlightChangedTo(int index) {}
  virtual void ActivatedAt(int index);
  virtual void MenuWillShow() {}

  // Overridden from SystemLibrary::Observer:
  virtual void TimezoneChanged(const icu::TimeZone& timezone);

  // Updates the time on the menu button. Can be called by host if timezone
  // changes.
  void UpdateText();

 private:
  // views::ViewMenuDelegate implementation.
  virtual void RunMenu(views::View* source, const gfx::Point& pt);

  // Updates text and schedules the timer to fire at the next minute interval.
  void UpdateTextAndSetNextTimer();

  base::OneShotTimer<ClockMenuButton> timer_;

  // The clock menu.
  // NOTE: we use a scoped_ptr here as menu calls into 'this' from the
  // constructor.
  scoped_ptr<views::Menu2> clock_menu_;

  StatusAreaHost* host_;

  DISALLOW_COPY_AND_ASSIGN(ClockMenuButton);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_STATUS_CLOCK_MENU_BUTTON_H_
