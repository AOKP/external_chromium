// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_TAB_CONTENTS_RENDER_VIEW_CONTEXT_MENU_GTK_H_
#define CHROME_BROWSER_TAB_CONTENTS_RENDER_VIEW_CONTEXT_MENU_GTK_H_
#pragma once

#include "base/scoped_ptr.h"
#include "chrome/browser/gtk/menu_gtk.h"
#include "chrome/browser/tab_contents/render_view_context_menu.h"
#include "gfx/point.h"

struct ContextMenuParams;

class RenderViewContextMenuGtk : public RenderViewContextMenu,
                                 public MenuGtk::Delegate {
 public:
  RenderViewContextMenuGtk(TabContents* web_contents,
                           const ContextMenuParams& params,
                           uint32_t triggering_event_time);

  ~RenderViewContextMenuGtk();

  // Show the menu at the given location.
  void Popup(const gfx::Point& point);

  // Menu::Delegate implementation ---------------------------------------------
  virtual void StoppedShowing();
  virtual bool AlwaysShowIconForCmd(int command_id) const;

 protected:
  // RenderViewContextMenu implementation --------------------------------------
  virtual void PlatformInit();
  // TODO(port): implement.
  virtual bool GetAcceleratorForCommandId(
      int command_id,
      menus::Accelerator* accelerator);

 private:
  scoped_ptr<MenuGtk> menu_gtk_;
  uint32_t triggering_event_time_;
};

#endif  // CHROME_BROWSER_TAB_CONTENTS_RENDER_VIEW_CONTEXT_MENU_GTK_H_
