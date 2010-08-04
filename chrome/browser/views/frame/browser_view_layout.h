// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_VIEWS_FRAME_BROWSER_VIEW_LAYOUT_H_
#define CHROME_BROWSER_VIEWS_FRAME_BROWSER_VIEW_LAYOUT_H_

#include "chrome/browser/views/frame/browser_view.h"
#include "views/layout_manager.h"

// The layout manager used in chrome browser.
class BrowserViewLayout : public views::LayoutManager {
 public:
  BrowserViewLayout();
  virtual ~BrowserViewLayout() {}

  // Returns the minimum size of the browser view.
  virtual gfx::Size GetMinimumSize();

  // Returns the bounding box for the find bar.
  virtual gfx::Rect GetFindBarBoundingBox() const;

  // Returns true if the specified point(BrowserView coordinates) is in
  // in the window caption area of the browser window.
  virtual bool IsPositionInWindowCaption(const gfx::Point& point);

  // Tests to see if the specified |point| (in nonclient view's coordinates)
  // is within the views managed by the laymanager. Returns one of
  // HitTestCompat enum defined in views/window/hit_test.h.
  // See also ClientView::NonClientHitTest.
  virtual int NonClientHitTest(const gfx::Point& point);

  // views::LayoutManager overrides:
  virtual void Installed(views::View* host);
  virtual void Uninstalled(views::View* host);
  virtual void ViewAdded(views::View* host, views::View* view);
  virtual void ViewRemoved(views::View* host, views::View* view);
  virtual void Layout(views::View* host);
  virtual gfx::Size GetPreferredSize(views::View* host);

 protected:
  Browser* browser() {
    return browser_view_->browser();
  }

  // Layout the TabStrip, returns the coordinate of the bottom of the TabStrip,
  // for laying out subsequent controls.
  virtual int LayoutTabStrip();

  // Layout the following controls, starting at |top|, returns the coordinate
  // of the bottom of the control, for laying out the next control.
  virtual int LayoutToolbar(int top);
  int LayoutBookmarkAndInfoBars(int top);
  int LayoutBookmarkBar(int top);
  int LayoutInfoBar(int top);

  // Layout the TabContents container, between the coordinates |top| and
  // |bottom|.
  void LayoutTabContents(int top, int bottom);
  int LayoutExtensionAndDownloadShelves();

  // Layout the Download Shelf, returns the coordinate of the top of the
  // control, for laying out the previous control.
  int LayoutDownloadShelf(int bottom);

  // Layout the Extension Shelf, returns the coordinate of the top of the
  // control, for laying out the previous control.
  int LayoutExtensionShelf(int bottom);

  // See description above vertical_layout_rect_ for details.
  void set_vertical_layout_rect(const gfx::Rect& bounds) {
    vertical_layout_rect_ = bounds;
  }
  const gfx::Rect& vertical_layout_rect() const {
    return vertical_layout_rect_;
  }

  // Child views that the layout manager manages.
  BaseTabStrip* tabstrip_;
  ToolbarView* toolbar_;
  views::View* contents_split_;
  views::View* contents_container_;
  views::View* infobar_container_;
  DownloadShelfView* download_shelf_;
  ExtensionShelf* extension_shelf_;
  BookmarkBarView* active_bookmark_bar_;

  BrowserView* browser_view_;

  // The bounds within which the vertically-stacked contents of the BrowserView
  // should be laid out within. When the SideTabstrip is not visible, this is
  // just the local bounds of the BrowserView, otherwise it's the local bounds
  // of the BrowserView less the width of the SideTabstrip.
  gfx::Rect vertical_layout_rect_;

  // The distance the FindBar is from the top of the window, in pixels.
  int find_bar_y_;

  DISALLOW_COPY_AND_ASSIGN(BrowserViewLayout);
};

#endif  // CHROME_BROWSER_VIEWS_FRAME_BROWSER_VIEW_LAYOUT_H_

