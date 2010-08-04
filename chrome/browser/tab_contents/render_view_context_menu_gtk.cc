// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/tab_contents/render_view_context_menu_gtk.h"

#include <gtk/gtk.h>

#include "base/string_util.h"
#include "chrome/app/chrome_dll_resource.h"
#include "chrome/browser/renderer_host/render_widget_host_view_gtk.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "webkit/glue/context_menu.h"

RenderViewContextMenuGtk::RenderViewContextMenuGtk(
    TabContents* web_contents,
    const ContextMenuParams& params,
    guint32 triggering_event_time)
    : RenderViewContextMenu(web_contents, params),
      triggering_event_time_(triggering_event_time) {
}

RenderViewContextMenuGtk::~RenderViewContextMenuGtk() {
}

void RenderViewContextMenuGtk::PlatformInit() {
  menu_gtk_.reset(new MenuGtk(this, &menu_model_));

  if (params_.is_editable) {
    RenderWidgetHostViewGtk* rwhv = static_cast<RenderWidgetHostViewGtk*>(
        source_tab_contents_->GetRenderWidgetHostView());
    if (rwhv)
      rwhv->AppendInputMethodsContextMenu(menu_gtk_.get());
  }
}

void RenderViewContextMenuGtk::Popup(const gfx::Point& point) {
  RenderWidgetHostView* rwhv = source_tab_contents_->GetRenderWidgetHostView();
  if (rwhv)
    rwhv->ShowingContextMenu(true);
  menu_gtk_->PopupAsContextAt(triggering_event_time_, point);
}

void RenderViewContextMenuGtk::StoppedShowing() {
  RenderWidgetHostView* rwhv = source_tab_contents_->GetRenderWidgetHostView();
  if (rwhv)
    rwhv->ShowingContextMenu(false);
}
