// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_VIEWS_TABS_TAB_H_
#define CHROME_BROWSER_VIEWS_TABS_TAB_H_

#include <string>

#include "base/scoped_ptr.h"
#include "chrome/browser/views/tabs/base_tab.h"
#include "gfx/point.h"

class MultiAnimation;
class SlideAnimation;

///////////////////////////////////////////////////////////////////////////////
//
// TabRenderer
//
//  A View that renders a Tab, either in a TabStrip or in a DraggedTabView.
//
///////////////////////////////////////////////////////////////////////////////
class Tab : public BaseTab {
 public:
  // The menu button's class name.
  static const char kViewClassName[];

  explicit Tab(TabController* controller);
  virtual ~Tab();

  // Start/stop the mini-tab title animation.
  void StartMiniTabTitleAnimation();
  void StopMiniTabTitleAnimation();

  // Set the background offset used to match the image in the inactive tab
  // to the frame image.
  void SetBackgroundOffset(const gfx::Point& offset) {
    background_offset_ = offset;
  }

  // Paints the icon. Most of the time you'll want to invoke Paint directly, but
  // in certain situations this invoked outside of Paint.
  void PaintIcon(gfx::Canvas* canvas);

  // Returns the minimum possible size of a single unselected Tab.
  static gfx::Size GetMinimumUnselectedSize();
  // Returns the minimum possible size of a selected Tab. Selected tabs must
  // always show a close button and have a larger minimum size than unselected
  // tabs.
  static gfx::Size GetMinimumSelectedSize();
  // Returns the preferred size of a single Tab, assuming space is
  // available.
  static gfx::Size GetStandardSize();

  // Returns the width for mini-tabs. Mini-tabs always have this width.
  static int GetMiniWidth();

  // Loads the images to be used for the tab background.
  static void LoadTabImages();

 protected:
  virtual const gfx::Rect& title_bounds() const { return title_bounds_; }

  // BaseTab overrides:
  virtual void DataChanged(const TabRendererData& old);

 private:
  // Overridden from views::View:
  virtual void Paint(gfx::Canvas* canvas);
  virtual void Layout();
  virtual void ThemeChanged();
  virtual std::string GetClassName() const { return kViewClassName; }
  virtual bool HasHitTestMask() const;
  virtual void GetHitTestMask(gfx::Path* path) const;
  virtual bool GetTooltipTextOrigin(const gfx::Point& p, gfx::Point* origin);

  // Paint various portions of the Tab
  void PaintTabBackground(gfx::Canvas* canvas);
  void PaintInactiveTabBackgroundWithTitleChange(gfx::Canvas* canvas);
  void PaintInactiveTabBackground(gfx::Canvas* canvas);
  void PaintActiveTabBackground(gfx::Canvas* canvas);

  // Returns the number of favicon-size elements that can fit in the tab's
  // current size.
  int IconCapacity() const;

  // Returns whether the Tab should display a favicon.
  bool ShouldShowIcon() const;

  // Returns whether the Tab should display a close button.
  bool ShouldShowCloseBox() const;

  // Gets the throb value for the tab. When a tab is not selected the
  // active background is drawn at |GetThrobValue()|%. This is used for hover,
  // mini tab title change and pulsing.
  double GetThrobValue();

  // The bounds of various sections of the display.
  gfx::Rect favicon_bounds_;
  gfx::Rect title_bounds_;

  // The offset used to paint the inactive background image.
  gfx::Point background_offset_;

  // Hover animation.
  scoped_ptr<SlideAnimation> hover_animation_;

  // Animation used when the title of an inactive mini tab changes.
  scoped_ptr<MultiAnimation> mini_title_animation_;

  struct TabImage {
    SkBitmap* image_l;
    SkBitmap* image_c;
    SkBitmap* image_r;
    int l_width;
    int r_width;
    int y_offset;
  };
  static TabImage tab_active;
  static TabImage tab_inactive;
  static TabImage tab_alpha;

  // Whether we're showing the icon. It is cached so that we can detect when it
  // changes and layout appropriately.
  bool showing_icon_;

  // Whether we are showing the close button. It is cached so that we can
  // detect when it changes and layout appropriately.
  bool showing_close_button_;

  // The current color of the close button.
  SkColor close_button_color_;

  static bool initialized_;

  DISALLOW_COPY_AND_ASSIGN(Tab);
};

#endif  // CHROME_BROWSER_VIEWS_TABS_TAB_H_
