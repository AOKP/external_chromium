// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "app/resource_bundle.h"
#include "base/string_util.h"
#include "chrome/browser/gtk/status_icons/status_icon_gtk.h"
#include "chrome/browser/gtk/status_icons/status_tray_gtk.h"
#include "grit/browser_resources.h"
#include "grit/theme_resources.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

class MockStatusIconObserver : public StatusIcon::Observer {
 public:
  MOCK_METHOD0(OnClicked, void());
};

TEST(StatusTrayGtkTest, CreateTray) {
  // Just tests creation/destruction.
  StatusTrayGtk tray;
}

TEST(StatusTrayGtkTest, CreateIcon) {
  // Create an icon, set the images and tooltip, then shut it down.
  StatusTrayGtk tray;
  StatusIcon* icon = tray.GetStatusIcon(ASCIIToUTF16("test"));
  SkBitmap* bitmap = ResourceBundle::GetSharedInstance().GetBitmapNamed(
      IDR_STATUS_TRAY_ICON);
  icon->SetImage(*bitmap);
  icon->SetPressedImage(*bitmap);
  icon->SetToolTip(ASCIIToUTF16("tool tip"));
}

TEST(StatusTrayGtkTest, ClickOnIcon) {
  // Create an icon, send a fake click event, make sure observer is called.
  StatusTrayGtk tray;
  StatusIconGtk* icon = static_cast<StatusIconGtk*>(
      tray.GetStatusIcon(ASCIIToUTF16("test")));
  MockStatusIconObserver observer;
  icon->AddObserver(&observer);
  EXPECT_CALL(observer, OnClicked());
  // Mimic a click.
  icon->OnClick(NULL);
  icon->RemoveObserver(&observer);
}

}  // namespace
