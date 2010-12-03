// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_SAD_TAB_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_SAD_TAB_VIEW_H_
#pragma once

#include "base/basictypes.h"
#include "gfx/font.h"
#include "views/controls/link.h"
#include "views/view.h"

class SkBitmap;
class TabContents;

///////////////////////////////////////////////////////////////////////////////
//
// SadTabView
//
//  A views::View subclass used to render the presentation of the crashed
//  "sad tab" in the browser window when a renderer is destroyed unnaturally.
//
///////////////////////////////////////////////////////////////////////////////
class SadTabView : public views::View,
                   public views::LinkController {
 public:
  explicit SadTabView(TabContents* tab_contents);
  virtual ~SadTabView() {}

  // Overridden from views::View:
  virtual void Paint(gfx::Canvas* canvas);
  virtual void Layout();

  // Overridden from views::LinkController:
  virtual void LinkActivated(views::Link* source, int event_flags);

 private:
  static void InitClass();

  // Assorted resources for display.
  static SkBitmap* sad_tab_bitmap_;
  static gfx::Font* title_font_;
  static gfx::Font* message_font_;
  static std::wstring title_;
  static std::wstring message_;
  static int title_width_;

  TabContents* tab_contents_;
  views::Link* learn_more_link_;

  // Regions within the display for different components, populated by
  // Layout().
  gfx::Rect icon_bounds_;
  gfx::Rect title_bounds_;
  gfx::Rect message_bounds_;
  gfx::Rect link_bounds_;

  DISALLOW_COPY_AND_ASSIGN(SadTabView);
};

#endif  // CHROME_BROWSER_UI_VIEWS_SAD_TAB_VIEW_H__
