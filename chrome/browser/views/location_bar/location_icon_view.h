// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_VIEWS_LOCATION_BAR_LOCATION_ICON_VIEW_H_
#define CHROME_BROWSER_VIEWS_LOCATION_BAR_LOCATION_ICON_VIEW_H_

#include "chrome/browser/views/location_bar/click_handler.h"
#include "views/controls/image_view.h"

class LocationBarView;
namespace views {
class MouseEvent;
}

// LocationIconView is used to display an icon to the left of the edit field.
// This shows the user's current action while editing, the page security
// status on https pages, or a globe for other URLs.
class LocationIconView : public views::ImageView {
 public:
  explicit LocationIconView(const LocationBarView* location_bar);
  virtual ~LocationIconView();

  // Overridden from view.
  virtual bool OnMousePressed(const views::MouseEvent& event);
  virtual void OnMouseReleased(const views::MouseEvent& event, bool canceled);

 private:
  ClickHandler click_handler_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(LocationIconView);
};

#endif  // CHROME_BROWSER_VIEWS_LOCATION_BAR_LOCATION_ICON_VIEW_H_
