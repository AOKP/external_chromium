// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_VIEWS_TAB_CONTENTS_RENDER_VIEW_CONTEXT_MENU_VIEWS_H_
#define CHROME_BROWSER_VIEWS_TAB_CONTENTS_RENDER_VIEW_CONTEXT_MENU_VIEWS_H_

#include "app/menus/simple_menu_model.h"
#include "base/scoped_ptr.h"
#include "base/scoped_vector.h"
#include "chrome/browser/tab_contents/render_view_context_menu.h"
#include "views/controls/menu/menu_2.h"

class RenderViewContextMenuViews : public RenderViewContextMenu {
 public:
  RenderViewContextMenuViews(TabContents* tab_contents,
                           const ContextMenuParams& params);

  virtual ~RenderViewContextMenuViews();

  void RunMenuAt(int x, int y);

  gfx::NativeMenu GetMenuHandle() const {
    return (menu_.get() ? menu_->GetNativeMenu() : NULL);
  }

#if defined(OS_WIN)
  // Set this menu to show for an external tab contents. This
  // only has an effect before Init() is called.
  void SetExternal();
#endif

  void UpdateMenuItemStates();

 protected:
  // RenderViewContextMenu implementation --------------------------------------
  virtual void PlatformInit();
  virtual bool GetAcceleratorForCommandId(int command_id,
                                          menus::Accelerator* accelerator);
 private:
  // The current radio group for radio menu items.
  int current_radio_group_id_;

  // The context menu itself and its contents.
  scoped_ptr<views::Menu2> menu_;

  DISALLOW_COPY_AND_ASSIGN(RenderViewContextMenuViews);
};

#endif  // CHROME_BROWSER_VIEWS_TAB_CONTENTS_RENDER_VIEW_CONTEXT_MENU_VIEWS_H_
