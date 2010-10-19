// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/notifications/balloon_host.h"

#include "chrome/browser/browser_list.h"
#include "chrome/browser/extensions/extension_process_manager.h"
#include "chrome/browser/notifications/balloon.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/renderer_host/render_view_host.h"
#include "chrome/browser/renderer_host/site_instance.h"
#include "chrome/browser/renderer_preferences_util.h"
#include "chrome/common/bindings_policy.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/notification_type.h"
#include "chrome/common/render_messages.h"
#include "chrome/common/renderer_preferences.h"
#include "chrome/common/url_constants.h"

BalloonHost::BalloonHost(Balloon* balloon)
    : render_view_host_(NULL),
      balloon_(balloon),
      initialized_(false),
      should_notify_on_disconnect_(false),
      enable_dom_ui_(false) {
  DCHECK(balloon_);

  // If the notification is for an extension URL, make sure to use the extension
  // process to render it, so that it can communicate with other views in the
  // extension.
  const GURL& balloon_url = balloon_->notification().content_url();
  if (balloon_url.SchemeIs(chrome::kExtensionScheme)) {
    site_instance_ =
      balloon_->profile()->GetExtensionProcessManager()->GetSiteInstanceForURL(
          balloon_url);
  } else {
    site_instance_ = SiteInstance::CreateSiteInstance(balloon_->profile());
  }
}

void BalloonHost::Shutdown() {
  if (render_view_host_) {
    render_view_host_->Shutdown();
    render_view_host_ = NULL;
  }
}

WebPreferences BalloonHost::GetWebkitPrefs() {
  WebPreferences prefs;
  prefs.allow_scripts_to_close_windows = true;
  return prefs;
}

void BalloonHost::Close(RenderViewHost* render_view_host) {
  balloon_->CloseByScript();
  NotifyDisconnect();
}

void BalloonHost::RenderViewCreated(RenderViewHost* render_view_host) {
  render_view_host->Send(new ViewMsg_DisableScrollbarsForSmallWindows(
      render_view_host->routing_id(), balloon_->min_scrollbar_size()));
  render_view_host->WasResized();
  render_view_host->EnablePreferredSizeChangedMode(
      kPreferredSizeWidth | kPreferredSizeHeightThisIsSlow);
}

void BalloonHost::RenderViewReady(RenderViewHost* render_view_host) {
  should_notify_on_disconnect_ = true;
  NotificationService::current()->Notify(
      NotificationType::NOTIFY_BALLOON_CONNECTED,
      Source<BalloonHost>(this), NotificationService::NoDetails());
}

void BalloonHost::RenderViewGone(RenderViewHost* render_view_host) {
  Close(render_view_host);
}

void BalloonHost::ProcessDOMUIMessage(
    const ViewHostMsg_DomMessage_Params& params) {
  if (extension_function_dispatcher_.get()) {
    extension_function_dispatcher_->HandleRequest(params);
  }
}

// RenderViewHostDelegate::View methods implemented to allow links to
// open pages in new tabs.
void BalloonHost::CreateNewWindow(
    int route_id,
    WindowContainerType window_container_type,
    const string16& frame_name) {
  delegate_view_helper_.CreateNewWindow(
      route_id,
      balloon_->profile(),
      site_instance_.get(),
      DOMUIFactory::GetDOMUIType(balloon_->profile(),
          balloon_->notification().content_url()),
      this,
      window_container_type,
      frame_name);
}

void BalloonHost::ShowCreatedWindow(int route_id,
                                    WindowOpenDisposition disposition,
                                    const gfx::Rect& initial_pos,
                                    bool user_gesture) {
  // Don't allow pop-ups from notifications.
  if (disposition == NEW_POPUP)
    return;

  TabContents* contents = delegate_view_helper_.GetCreatedWindow(route_id);
  if (!contents)
    return;
  Browser* browser = BrowserList::GetLastActiveWithProfile(balloon_->profile());
  if (!browser)
    return;

  browser->AddTabContents(contents, disposition, initial_pos, user_gesture);
}

void BalloonHost::UpdatePreferredSize(const gfx::Size& new_size) {
  balloon_->SetContentPreferredSize(new_size);
}

void BalloonHost::HandleMouseDown() {
  balloon_->OnClick();
}

RendererPreferences BalloonHost::GetRendererPrefs(Profile* profile) const {
  RendererPreferences preferences;
  renderer_preferences_util::UpdateFromSystemSettings(&preferences, profile);
  return preferences;
}

void BalloonHost::Init() {
  DCHECK(!render_view_host_) << "BalloonViewHost already initialized.";
  RenderViewHost* rvh = new RenderViewHost(
      site_instance_.get(), this, MSG_ROUTING_NONE, NULL);
  if (GetProfile()->GetExtensionsService()) {
    extension_function_dispatcher_.reset(
        ExtensionFunctionDispatcher::Create(
            rvh, this, balloon_->notification().content_url()));
  }
  if (extension_function_dispatcher_.get()) {
    rvh->AllowBindings(BindingsPolicy::EXTENSION);
    rvh->set_is_extension_process(true);
  } else if (enable_dom_ui_) {
    rvh->AllowBindings(BindingsPolicy::DOM_UI);
  }

  // Do platform-specific initialization.
  render_view_host_ = rvh;
  InitRenderWidgetHostView();
  DCHECK(render_widget_host_view());

  rvh->set_view(render_widget_host_view());
  rvh->CreateRenderView(string16());
  rvh->NavigateToURL(balloon_->notification().content_url());

  initialized_ = true;
}

void BalloonHost::EnableDOMUI() {
  DCHECK(render_view_host_ == NULL) <<
      "EnableDOMUI has to be called before a renderer is created.";
  enable_dom_ui_ = true;
}

void BalloonHost::NotifyDisconnect() {
  if (!should_notify_on_disconnect_)
    return;

  should_notify_on_disconnect_ = false;
  NotificationService::current()->Notify(
      NotificationType::NOTIFY_BALLOON_DISCONNECTED,
      Source<BalloonHost>(this), NotificationService::NoDetails());
}
