// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/notifications/balloon_host.h"

#include "chrome/browser/browser_list.h"
#include "chrome/browser/extensions/extension_process_manager.h"
#include "chrome/browser/notifications/balloon.h"
#include "chrome/browser/notifications/notification.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/renderer_host/render_view_host.h"
#include "chrome/browser/renderer_host/site_instance.h"
#include "chrome/browser/renderer_preferences_util.h"
#include "chrome/common/bindings_policy.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/notification_type.h"
#include "chrome/common/render_messages.h"
#include "chrome/common/renderer_preferences.h"
#include "chrome/common/url_constants.h"
#include "webkit/glue/webpreferences.h"

namespace {
class BalloonPaintObserver : public RenderWidgetHost::PaintObserver {
 public:
  explicit BalloonPaintObserver(BalloonHost* balloon_host)
      : balloon_host_(balloon_host) {
  }

  virtual void RenderWidgetHostWillPaint(RenderWidgetHost* rhw) {}
  virtual void RenderWidgetHostDidPaint(RenderWidgetHost* rwh);

 private:
  BalloonHost* balloon_host_;

  DISALLOW_COPY_AND_ASSIGN(BalloonPaintObserver);
};

void BalloonPaintObserver::RenderWidgetHostDidPaint(RenderWidgetHost* rwh) {
  balloon_host_->RenderWidgetHostDidPaint();
  // WARNING: we may have been deleted (if the balloon host cleared the paint
  // observer).
}

}  // namespace

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
  NotifyDisconnect();
  if (render_view_host_) {
    render_view_host_->Shutdown();
    render_view_host_ = NULL;
  }
}

Browser* BalloonHost::GetBrowser() const {
  // Notifications aren't associated with a particular browser.
  return NULL;
}

gfx::NativeView BalloonHost::GetNativeViewOfHost() {
  // TODO(aa): Should this return the native view of the BalloonView*?
  return NULL;
}

TabContents* BalloonHost::associated_tab_contents() const { return NULL; }

const string16& BalloonHost::GetSource() const {
  return balloon_->notification().display_source();
}

WebPreferences BalloonHost::GetWebkitPrefs() {
  WebPreferences web_prefs =
      RenderViewHostDelegateHelper::GetWebkitPrefs(GetProfile(),
                                                   enable_dom_ui_);
  web_prefs.allow_scripts_to_close_windows = true;
  return web_prefs;
}

SiteInstance* BalloonHost::GetSiteInstance() const {
  return site_instance_.get();
}

Profile* BalloonHost::GetProfile() const {
  return balloon_->profile();
}

const GURL& BalloonHost::GetURL() const {
  return balloon_->notification().content_url();
}

void BalloonHost::Close(RenderViewHost* render_view_host) {
  balloon_->CloseByScript();
  NotifyDisconnect();
}

void BalloonHost::RenderViewCreated(RenderViewHost* render_view_host) {
  render_view_host->Send(new ViewMsg_DisableScrollbarsForSmallWindows(
      render_view_host->routing_id(), balloon_->min_scrollbar_size()));
  render_view_host->WasResized();
#if !defined(OS_MACOSX)
  // TODO(levin): Make all of the code that went in originally with this change
  // to be cross-platform. See http://crbug.com/64720
  render_view_host->EnablePreferredSizeChangedMode(
      kPreferredSizeWidth | kPreferredSizeHeightThisIsSlow);
#endif
}

void BalloonHost::RenderViewReady(RenderViewHost* render_view_host) {
  should_notify_on_disconnect_ = true;
  NotificationService::current()->Notify(
      NotificationType::NOTIFY_BALLOON_CONNECTED,
      Source<BalloonHost>(this), NotificationService::NoDetails());
}

void BalloonHost::RenderViewGone(RenderViewHost* render_view_host,
                                 base::TerminationStatus status,
                                 int error_code) {
  Close(render_view_host);
}

int BalloonHost::GetBrowserWindowID() const {
  return extension_misc::kUnknownWindowId;
}

ViewType::Type BalloonHost::GetRenderViewType() const {
  return ViewType::NOTIFICATION;
}

RenderViewHostDelegate::View* BalloonHost::GetViewDelegate() {
  return this;
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

bool BalloonHost::PreHandleKeyboardEvent(const NativeWebKeyboardEvent& event,
                                         bool* is_keyboard_shortcut) {
  return false;
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
  if (GetProfile()->GetExtensionService()) {
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
#if defined(OS_MACOSX)
  rvh->set_paint_observer(new BalloonPaintObserver(this));
#endif
  rvh->NavigateToURL(balloon_->notification().content_url());

  initialized_ = true;
}

void BalloonHost::EnableDOMUI() {
  DCHECK(render_view_host_ == NULL) <<
      "EnableDOMUI has to be called before a renderer is created.";
  enable_dom_ui_ = true;
}

void BalloonHost::UpdateInspectorSetting(const std::string& key,
                                         const std::string& value) {
  RenderViewHostDelegateHelper::UpdateInspectorSetting(
      GetProfile(), key, value);
}

void BalloonHost::ClearInspectorSettings() {
  RenderViewHostDelegateHelper::ClearInspectorSettings(GetProfile());
}

void BalloonHost::RenderWidgetHostDidPaint() {
  render_view_host_->set_paint_observer(NULL);
  render_view_host_->EnablePreferredSizeChangedMode(
      kPreferredSizeWidth | kPreferredSizeHeightThisIsSlow);
}

BalloonHost::~BalloonHost() {
  DCHECK(!render_view_host_);
}

void BalloonHost::NotifyDisconnect() {
  if (!should_notify_on_disconnect_)
    return;

  should_notify_on_disconnect_ = false;
  NotificationService::current()->Notify(
      NotificationType::NOTIFY_BALLOON_DISCONNECTED,
      Source<BalloonHost>(this), NotificationService::NoDetails());
}
