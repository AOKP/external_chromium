// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_VIEWS_TABS_SIDE_TAB_H_
#define CHROME_BROWSER_VIEWS_TABS_SIDE_TAB_H_

#include "app/slide_animation.h"
#include "chrome/browser/views/tabs/base_tab.h"
#include "gfx/font.h"

class SideTab;
class TabStripController;

class SideTab : public BaseTab {
 public:
  explicit SideTab(TabController* controller);
  virtual ~SideTab();

  // Returns the preferred height of side tabs.
  static int GetPreferredHeight();

  // views::View Overrides:
  virtual void Layout();
  virtual void Paint(gfx::Canvas* canvas);
  virtual gfx::Size GetPreferredSize();

 protected:
  virtual const gfx::Rect& title_bounds() const { return title_bounds_; }

  // Returns true if the selected highlight should be rendered.
  virtual bool ShouldPaintHighlight() const;

 private:
  // Returns true if the icon should be shown.
  bool ShouldShowIcon() const;

  gfx::Rect icon_bounds_;
  gfx::Rect title_bounds_;

  DISALLOW_COPY_AND_ASSIGN(SideTab);
};

#endif  // CHROME_BROWSER_VIEWS_TABS_SIDE_TAB_H_
