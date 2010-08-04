// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/renderer_host/test/test_render_view_host.h"

#include "chrome/browser/browser_url_handler.h"
#include "chrome/browser/renderer_host/test/test_backing_store.h"
#include "chrome/browser/tab_contents/test_tab_contents.h"
#include "chrome/common/dom_storage_common.h"
#include "chrome/common/render_messages.h"
#include "gfx/rect.h"
#include "webkit/glue/webpreferences.h"

using webkit_glue::PasswordForm;

void InitNavigateParams(ViewHostMsg_FrameNavigate_Params* params,
                        int page_id,
                        const GURL& url,
                        PageTransition::Type transition) {
  params->page_id = page_id;
  params->url = url;
  params->referrer = GURL();
  params->transition = transition;
  params->redirects = std::vector<GURL>();
  params->should_update_history = false;
  params->searchable_form_url = GURL();
  params->searchable_form_encoding = std::string();
  params->password_form = PasswordForm();
  params->security_info = std::string();
  params->gesture = NavigationGestureUser;
  params->is_post = false;
}

TestRenderViewHost::TestRenderViewHost(SiteInstance* instance,
                                       RenderViewHostDelegate* delegate,
                                       int routing_id)
    : RenderViewHost(instance, delegate, routing_id,
                     kInvalidSessionStorageNamespaceId),
      render_view_created_(false),
      delete_counter_(NULL) {
  set_view(new TestRenderWidgetHostView(this));
}

TestRenderViewHost::~TestRenderViewHost() {
  if (delete_counter_)
    ++*delete_counter_;

  // Since this isn't a traditional view, we have to delete it.
  delete view();
}

bool TestRenderViewHost::CreateRenderView(
    URLRequestContextGetter* request_context, const string16& frame_name) {
  DCHECK(!render_view_created_);
  render_view_created_ = true;
  process()->ViewCreated();
  return true;
}

bool TestRenderViewHost::IsRenderViewLive() const {
  return render_view_created_;
}

void TestRenderViewHost::TestOnMessageReceived(const IPC::Message& msg) {
  OnMessageReceived(msg);
}

void TestRenderViewHost::SendNavigate(int page_id, const GURL& url) {
  SendNavigateWithTransition(page_id, url, PageTransition::LINK);
}

void TestRenderViewHost::SendNavigateWithTransition(
    int page_id, const GURL& url, PageTransition::Type transition) {
  ViewHostMsg_FrameNavigate_Params params;

  params.page_id = page_id;
  params.url = url;
  params.referrer = GURL();
  params.transition = transition;
  params.redirects = std::vector<GURL>();
  params.should_update_history = true;
  params.searchable_form_url = GURL();
  params.searchable_form_encoding = std::string();
  params.password_form = PasswordForm();
  params.security_info = std::string();
  params.gesture = NavigationGestureUser;
  params.contents_mime_type = std::string();
  params.is_post = false;
  params.is_content_filtered = false;
  params.http_status_code = 0;

  ViewHostMsg_FrameNavigate msg(1, params);
  OnMsgNavigate(msg);
}

TestRenderWidgetHostView::TestRenderWidgetHostView(RenderWidgetHost* rwh)
    : rwh_(rwh),
      is_showing_(false) {
}

BackingStore* TestRenderWidgetHostView::AllocBackingStore(
    const gfx::Size& size) {
  return new TestBackingStore(rwh_, size);
}

VideoLayer* TestRenderWidgetHostView::AllocVideoLayer(
    const gfx::Size& size) {
  NOTIMPLEMENTED();
  return NULL;
}

#if defined(OS_MACOSX)
gfx::Rect TestRenderWidgetHostView::GetWindowRect() {
  return gfx::Rect();
}

gfx::Rect TestRenderWidgetHostView::GetRootWindowRect() {
  return gfx::Rect();
}

void TestRenderWidgetHostView::SetActive(bool active) {
  // <viettrungluu@gmail.com>: Do I need to do anything here?
}

gfx::PluginWindowHandle
TestRenderWidgetHostView::AllocateFakePluginWindowHandle(bool opaque) {
  return NULL;
}

void TestRenderWidgetHostView::DestroyFakePluginWindowHandle(
    gfx::PluginWindowHandle window) {
}

void TestRenderWidgetHostView::AcceleratedSurfaceSetIOSurface(
    gfx::PluginWindowHandle window,
    int32 width,
    int32 height,
    uint64 io_surface_identifier) {
}

void TestRenderWidgetHostView::AcceleratedSurfaceSetTransportDIB(
    gfx::PluginWindowHandle window,
    int32 width,
    int32 height,
    TransportDIB::Handle transport_dib) {
}

void TestRenderWidgetHostView::AcceleratedSurfaceBuffersSwapped(
    gfx::PluginWindowHandle window) {
}

void TestRenderWidgetHostView::DrawAcceleratedSurfaceInstances(
    CGLContextObj context) {
}
#endif

void RenderViewHostTestHarness::NavigateAndCommit(const GURL& url) {
  controller().LoadURL(url, GURL(), 0);
  GURL loaded_url(url);
  bool reverse_on_redirect = false;
  BrowserURLHandler::RewriteURLIfNecessary(
      &loaded_url, profile(), &reverse_on_redirect);
  rvh()->SendNavigate(process()->max_page_id() + 1, loaded_url);
}

void RenderViewHostTestHarness::Reload() {
  NavigationEntry* entry = controller().GetLastCommittedEntry();
  DCHECK(entry);
  controller().Reload(false);
  rvh()->SendNavigate(entry->page_id(), entry->url());
}

void RenderViewHostTestHarness::SetUp() {
  // See comment above profile_ decl for why we check for NULL here.
  if (!profile_.get())
    profile_.reset(new TestingProfile());

  // This will be deleted when the TabContents goes away.
  SiteInstance* instance = SiteInstance::CreateSiteInstance(profile_.get());

  contents_.reset(new TestTabContents(profile_.get(), instance));
}

void RenderViewHostTestHarness::TearDown() {
  contents_.reset();

  // Make sure that we flush any messages related to TabContents destruction
  // before we destroy the profile.
  MessageLoop::current()->RunAllPending();

  // Release the profile on the UI thread.
  message_loop_.DeleteSoon(FROM_HERE, profile_.release());
  message_loop_.RunAllPending();
}
