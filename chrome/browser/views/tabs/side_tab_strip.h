// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_VIEWS_TABS_SIDE_TAB_STRIP_H_
#define CHROME_BROWSER_VIEWS_TABS_SIDE_TAB_STRIP_H_
#pragma once

#include "chrome/browser/views/tabs/base_tab_strip.h"

struct TabRendererData;

class SideTabStrip : public BaseTabStrip {
 public:
  // The tabs are inset by this much along all axis.
  static const int kTabStripInset;

  explicit SideTabStrip(TabStripController* controller);
  virtual ~SideTabStrip();

  // BaseTabStrip implementation:
  virtual int GetPreferredHeight();
  virtual void SetBackgroundOffset(const gfx::Point& offset);
  virtual bool IsPositionInWindowCaption(const gfx::Point& point);
  virtual void SetDraggedTabBounds(int tab_index,
                                   const gfx::Rect& tab_bounds);
  virtual TabStrip* AsTabStrip();

  virtual void StartHighlight(int model_index);
  virtual void StopAllHighlighting();
  virtual BaseTab* CreateTabForDragging();
  virtual void RemoveTabAt(int model_index);
  virtual void SelectTabAt(int old_model_index, int new_model_index);
  virtual void TabTitleChangedNotLoading(int model_index);

  // views::View overrides:
  virtual gfx::Size GetPreferredSize();
  virtual void PaintChildren(gfx::Canvas* canvas);

 protected:
  // BaseTabStrip overrides:
  virtual BaseTab* CreateTab();
  virtual void GenerateIdealBounds();
  virtual void StartInsertTabAnimation(int model_index, bool foreground);
  virtual void StartMoveTabAnimation();
  virtual void StopAnimating(bool layout);
  virtual void AnimateToIdealBounds();
  virtual void Layout();

 private:
  // The "New Tab" button.
  views::View* newtab_button_;

  // Ideal bounds of the new tab button.
  gfx::Rect newtab_button_bounds_;

  // Separator between mini-tabs and the new tab button. The separator is
  // positioned above the visible area if there are no mini-tabs.
  views::View* separator_;

  // Bounds of the sepatator.
  gfx::Rect separator_bounds_;

  DISALLOW_COPY_AND_ASSIGN(SideTabStrip);
};

#endif  // CHROME_BROWSER_VIEWS_TABS_SIDE_TAB_STRIP_H_
