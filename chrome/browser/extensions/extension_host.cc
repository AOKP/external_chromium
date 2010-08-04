// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_host.h"

#include <list>

#include "app/l10n_util.h"
#include "app/resource_bundle.h"
#include "base/keyboard_codes.h"
#include "base/message_loop.h"
#include "base/singleton.h"
#include "base/string_util.h"
#include "chrome/browser/browser.h"
#include "chrome/browser/browser_list.h"
#include "chrome/browser/browser_shutdown.h"
#include "chrome/browser/browser_theme_provider.h"
#include "chrome/browser/browsing_instance.h"
#include "chrome/browser/debugger/devtools_manager.h"
#include "chrome/browser/dom_ui/dom_ui_factory.h"
#include "chrome/browser/extensions/extension_message_service.h"
#include "chrome/browser/extensions/extension_tabs_module.h"
#include "chrome/browser/extensions/extension_tabs_module_constants.h"
#include "chrome/browser/extensions/extensions_service.h"
#include "chrome/browser/in_process_webkit/dom_storage_context.h"
#include "chrome/browser/in_process_webkit/webkit_context.h"
#include "chrome/browser/message_box_handler.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/pref_service.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/renderer_host/render_view_host.h"
#include "chrome/browser/renderer_host/render_process_host.h"
#include "chrome/browser/renderer_host/render_widget_host.h"
#include "chrome/browser/renderer_host/render_widget_host_view.h"
#include "chrome/browser/renderer_host/site_instance.h"
#include "chrome/browser/renderer_preferences_util.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/browser/tab_contents/tab_contents_view.h"
#include "chrome/common/bindings_policy.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/view_types.h"
#include "chrome/common/render_messages.h"
#include "chrome/common/url_constants.h"
#include "grit/browser_resources.h"
#include "grit/generated_resources.h"
#include "webkit/glue/context_menu.h"

#if defined(TOOLKIT_VIEWS)
#include "views/widget/widget.h"
#endif

using WebKit::WebDragOperation;
using WebKit::WebDragOperationsMask;

// static
bool ExtensionHost::enable_dom_automation_ = false;

static const char* kToolstripTextColorSubstitution = "$TEXT_COLOR$";

// Helper class that rate-limits the creation of renderer processes for
// ExtensionHosts, to avoid blocking the UI.
class ExtensionHost::ProcessCreationQueue {
 public:
  static ProcessCreationQueue* get() {
    return Singleton<ProcessCreationQueue>::get();
  }

  // Add a host to the queue for RenderView creation.
  void CreateSoon(ExtensionHost* host) {
    queue_.push_back(host);
    PostTask();
  }

  // Remove a host from the queue (in case it's being deleted).
  void Remove(ExtensionHost* host) {
    Queue::iterator it = std::find(queue_.begin(), queue_.end(), host);
    if (it != queue_.end())
      queue_.erase(it);
  }

 private:
  friend class Singleton<ProcessCreationQueue>;
  friend struct DefaultSingletonTraits<ProcessCreationQueue>;
  ProcessCreationQueue()
      : pending_create_(false),
        ALLOW_THIS_IN_INITIALIZER_LIST(method_factory_(this)) { }

  // Queue up a delayed task to process the next ExtensionHost in the queue.
  void PostTask() {
    if (!pending_create_) {
      MessageLoop::current()->PostTask(FROM_HERE,
          method_factory_.NewRunnableMethod(
             &ProcessCreationQueue::ProcessOneHost));
      pending_create_ = true;
    }
  }

  // Create the RenderView for the next host in the queue.
  void ProcessOneHost() {
    pending_create_ = false;
    if (queue_.empty())
      return;  // can happen on shutdown

    queue_.front()->CreateRenderViewNow();
    queue_.pop_front();

    if (!queue_.empty())
      PostTask();
  }

  typedef std::list<ExtensionHost*> Queue;
  Queue queue_;
  bool pending_create_;
  ScopedRunnableMethodFactory<ProcessCreationQueue> method_factory_;
};

////////////////
// ExtensionHost

ExtensionHost::ExtensionHost(Extension* extension, SiteInstance* site_instance,
                             const GURL& url, ViewType::Type host_type)
    : extension_(extension),
      profile_(site_instance->browsing_instance()->profile()),
      did_stop_loading_(false),
      document_element_available_(false),
      url_(url),
      extension_host_type_(host_type),
      associated_tab_contents_(NULL) {
  int64 session_storage_namespace_id = profile_->GetWebKitContext()->
      dom_storage_context()->AllocateSessionStorageNamespaceId();
  render_view_host_ = new RenderViewHost(site_instance, this, MSG_ROUTING_NONE,
                                         session_storage_namespace_id);
  render_view_host_->set_is_extension_process(true);
  render_view_host_->AllowBindings(BindingsPolicy::EXTENSION);
  if (enable_dom_automation_)
    render_view_host_->AllowBindings(BindingsPolicy::DOM_AUTOMATION);

  // Listen for when the render process' handle is available so we can add it
  // to the task manager then.
  registrar_.Add(this, NotificationType::RENDERER_PROCESS_CREATED,
                 Source<RenderProcessHost>(render_process_host()));
  // Listen for when an extension is unloaded from the same profile, as it may
  // be the same extension that this points to.
  registrar_.Add(this, NotificationType::EXTENSION_UNLOADED,
                 Source<Profile>(profile_));
}

ExtensionHost::~ExtensionHost() {
  NotificationService::current()->Notify(
      NotificationType::EXTENSION_HOST_DESTROYED,
      Source<Profile>(profile_),
      Details<ExtensionHost>(this));
  ProcessCreationQueue::get()->Remove(this);
  render_view_host_->Shutdown();  // deletes render_view_host
}

void ExtensionHost::CreateView(Browser* browser) {
#if defined(TOOLKIT_VIEWS)
  view_.reset(new ExtensionView(this, browser));
  // We own |view_|, so don't auto delete when it's removed from the view
  // hierarchy.
  view_->set_parent_owned(false);
#elif defined(OS_MACOSX)
  view_.reset(new ExtensionViewMac(this, browser));
  view_->Init();
#elif defined(TOOLKIT_USES_GTK)
  view_.reset(new ExtensionViewGtk(this, browser));
  view_->Init();
#else
  // TODO(port)
  NOTREACHED();
#endif
}

RenderProcessHost* ExtensionHost::render_process_host() const {
  return render_view_host_->process();
}

SiteInstance* ExtensionHost::site_instance() const {
  return render_view_host_->site_instance();
}

bool ExtensionHost::IsRenderViewLive() const {
  return render_view_host_->IsRenderViewLive();
}

void ExtensionHost::CreateRenderViewSoon(RenderWidgetHostView* host_view) {
  LOG(INFO) << "Creating RenderView for " + extension_->name();
  render_view_host_->set_view(host_view);
  if (render_view_host_->process()->HasConnection()) {
    // If the process is already started, go ahead and initialize the RenderView
    // synchronously. The process creation is the real meaty part that we want
    // to defer.
    CreateRenderViewNow();
  } else {
    ProcessCreationQueue::get()->CreateSoon(this);
  }
}

void ExtensionHost::CreateRenderViewNow() {
  render_view_host_->CreateRenderView(profile_->GetRequestContext(),
                                      string16());
  NavigateToURL(url_);
  DCHECK(IsRenderViewLive());
}

void ExtensionHost::NavigateToURL(const GURL& url) {
  LOG(INFO) << "Request to NavigateToURL " << url.spec() << " for "
      << extension_->name();
  // Prevent explicit navigation to another extension id's pages.
  // This method is only called by some APIs, so we still need to protect
  // DidNavigate below (location = "").
  if (url.SchemeIs(chrome::kExtensionScheme) &&
      url.host() != extension_->id()) {
    // TODO(erikkay) communicate this back to the caller?
    return;
  }

  url_ = url;

  if (!is_background_page() && !extension_->GetBackgroundPageReady()) {
    LOG(INFO) << "...Waiting on EXTENSION_BACKGROUND_PAGE_READY";
    // Make sure the background page loads before any others.
    registrar_.Add(this, NotificationType::EXTENSION_BACKGROUND_PAGE_READY,
                   Source<Extension>(extension_));
    return;
  }

  LOG(INFO) << "Navigating to " << url_.spec();
  render_view_host_->NavigateToURL(url_);
}

void ExtensionHost::Observe(NotificationType type,
                            const NotificationSource& source,
                            const NotificationDetails& details) {
  switch (type.value) {
    case NotificationType::EXTENSION_BACKGROUND_PAGE_READY:
      DCHECK(extension_->GetBackgroundPageReady());
      NavigateToURL(url_);
      break;
    case NotificationType::BROWSER_THEME_CHANGED:
      if (extension_host_type_ == ViewType::EXTENSION_TOOLSTRIP ||
          extension_host_type_ == ViewType::EXTENSION_MOLE) {
        InsertThemedToolstripCSS();
      }
      break;
    case NotificationType::RENDERER_PROCESS_CREATED:
      LOG(INFO) << "Sending EXTENSION_PROCESS_CREATED";
      NotificationService::current()->Notify(
          NotificationType::EXTENSION_PROCESS_CREATED,
          Source<Profile>(profile_),
          Details<ExtensionHost>(this));
      break;
    case NotificationType::EXTENSION_UNLOADED:
      // The extension object will be deleted after this notification has been
      // sent. NULL it out so that dirty pointer issues don't arise in cases
      // when multiple ExtensionHost objects pointing to the same Extension are
      // present.
      if (extension_ == Details<Extension>(details).ptr())
        extension_ = NULL;
      break;
    default:
      NOTREACHED() << "Unexpected notification sent.";
      break;
  }
}

void ExtensionHost::UpdatePreferredSize(const gfx::Size& new_size) {
  if (view_.get())
    view_->UpdatePreferredSize(new_size);
}

void ExtensionHost::RenderViewGone(RenderViewHost* render_view_host) {
  // During browser shutdown, we may use sudden termination on an extension
  // process, so it is expected to lose our connection to the render view.
  // Do nothing.
  if (browser_shutdown::GetShutdownType() != browser_shutdown::NOT_VALID)
    return;

  // In certain cases, multiple ExtensionHost objects may have pointed to
  // the same Extension at some point (one with a background page and a
  // popup, for example). When the first ExtensionHost goes away, the extension
  // is unloaded, and any other host that pointed to that extension will have
  // its pointer to it NULLed out so that any attempt to unload a dirty pointer
  // will be averted.
  if (!extension_)
    return;

  LOG(INFO) << "Sending EXTENSION_PROCESS_TERMINATED for " + extension_->name();
  DCHECK_EQ(render_view_host_, render_view_host);
  NotificationService::current()->Notify(
      NotificationType::EXTENSION_PROCESS_TERMINATED,
      Source<Profile>(profile_),
      Details<ExtensionHost>(this));
}

void ExtensionHost::DidNavigate(RenderViewHost* render_view_host,
    const ViewHostMsg_FrameNavigate_Params& params) {
  // We only care when the outer frame changes.
  if (!PageTransition::IsMainFrame(params.transition))
    return;

  if (!params.url.SchemeIs(chrome::kExtensionScheme)) {
    extension_function_dispatcher_.reset(NULL);
    url_ = params.url;
    return;
  }

  // This catches two bogus use cases:
  // (1) URLs that look like chrome-extension://somethingbogus or
  //     chrome-extension://nosuchid/, in other words, no Extension would
  //     be found.
  // (2) URLs that refer to a different extension than this one.
  // In both cases, we preserve the old URL and reset the EFD to NULL.  This
  // will leave the host in kind of a bad state with poor UI and errors, but
  // it's better than the alternative.
  // TODO(erikkay) Perhaps we should display log errors or display a big 404
  // in the toolstrip or something like that.
  if (params.url.host() != extension_->id()) {
    extension_function_dispatcher_.reset(NULL);
    return;
  }

  LOG(INFO) << "(DidNavigate) Resetting EFD to " << url_.spec() << " for "
      << extension_->name();
  url_ = params.url;
  extension_function_dispatcher_.reset(
      ExtensionFunctionDispatcher::Create(render_view_host_, this, url_));
}

void ExtensionHost::InsertInfobarCSS() {
  DCHECK(!is_background_page());

  static const base::StringPiece css(
      ResourceBundle::GetSharedInstance().GetRawDataResource(
      IDR_EXTENSIONS_INFOBAR_CSS));

  render_view_host()->InsertCSSInWebFrame(
      L"", css.as_string(), "InfobarThemeCSS");
}

void ExtensionHost::InsertThemedToolstripCSS() {
  DCHECK(!is_background_page());

  static const base::StringPiece toolstrip_theme_css(
      ResourceBundle::GetSharedInstance().GetRawDataResource(
      IDR_EXTENSIONS_TOOLSTRIP_THEME_CSS));

  std::string css = toolstrip_theme_css.as_string();
  ThemeProvider* theme_provider =
      render_view_host()->process()->profile()->GetThemeProvider();

  SkColor text_color = theme_provider ?
      theme_provider->GetColor(BrowserThemeProvider::COLOR_BOOKMARK_TEXT) :
      SK_ColorBLACK;

  std::string hex_color_string = StringPrintf(
      "#%02x%02x%02x", SkColorGetR(text_color),
                       SkColorGetG(text_color),
                       SkColorGetB(text_color));
  size_t pos = css.find(kToolstripTextColorSubstitution);
  while (pos != std::string::npos) {
    css.replace(pos, 12, hex_color_string);
    pos = css.find(kToolstripTextColorSubstitution);
  }

  // As a toolstrip, inject our toolstrip CSS to make it easier for toolstrips
  // to blend in with the chrome UI.
  render_view_host()->InsertCSSInWebFrame(L"", css, "ToolstripThemeCSS");
}

void ExtensionHost::DisableScrollbarsForSmallWindows(
    const gfx::Size& size_limit) {
  render_view_host()->Send(new ViewMsg_DisableScrollbarsForSmallWindows(
      render_view_host()->routing_id(), size_limit));
}

void ExtensionHost::DidStopLoading() {
  bool notify = !did_stop_loading_;
  did_stop_loading_ = true;
  if (extension_host_type_ == ViewType::EXTENSION_TOOLSTRIP ||
      extension_host_type_ == ViewType::EXTENSION_MOLE ||
      extension_host_type_ == ViewType::EXTENSION_POPUP ||
      extension_host_type_ == ViewType::EXTENSION_INFOBAR) {
#if defined(TOOLKIT_VIEWS)
    if (view_.get())
      view_->DidStopLoading();
#endif
  }
  if (notify) {
    LOG(INFO) << "Sending EXTENSION_HOST_DID_STOP_LOADING";
    NotificationService::current()->Notify(
        NotificationType::EXTENSION_HOST_DID_STOP_LOADING,
        Source<Profile>(profile_),
        Details<ExtensionHost>(this));
    if (extension_host_type_ == ViewType::EXTENSION_BACKGROUND_PAGE) {
      UMA_HISTOGRAM_TIMES("Extensions.BackgroundPageLoadTime",
                          since_created_.Elapsed());
    } else if (extension_host_type_ == ViewType::EXTENSION_POPUP) {
      UMA_HISTOGRAM_TIMES("Extensions.PopupLoadTime",
                          since_created_.Elapsed());
    } else if (extension_host_type_ == ViewType::EXTENSION_TOOLSTRIP) {
      UMA_HISTOGRAM_TIMES("Extensions.ToolstripLoadTime",
                          since_created_.Elapsed());
    } else if (extension_host_type_ == ViewType::EXTENSION_INFOBAR) {
      UMA_HISTOGRAM_TIMES("Extensions.InfobarLoadTime",
        since_created_.Elapsed());
    }
  }
}

void ExtensionHost::DocumentAvailableInMainFrame(RenderViewHost* rvh) {
  // If the document has already been marked as available for this host, then
  // bail. No need for the redundant setup. http://crbug.com/31170
  if (document_element_available_)
    return;

  document_element_available_ = true;
  if (is_background_page()) {
    extension_->SetBackgroundPageReady();
  } else {
    switch (extension_host_type_) {
      case ViewType::EXTENSION_INFOBAR:
        InsertInfobarCSS();
        break;
      case ViewType::EXTENSION_TOOLSTRIP:
      case ViewType::EXTENSION_MOLE:
        // See also BROWSER_THEME_CHANGED in the Observe function.
        InsertThemedToolstripCSS();

        // Listen for browser changes so we can resend the CSS.
        registrar_.Add(this, NotificationType::BROWSER_THEME_CHANGED,
                       NotificationService::AllSources());
        break;
      default:
        break;  // No style sheet for other types, at the moment.
    }
  }
}

void ExtensionHost::DocumentOnLoadCompletedInMainFrame(RenderViewHost* rvh) {
  if (ViewType::EXTENSION_POPUP == GetRenderViewType()) {
    NotificationService::current()->Notify(
        NotificationType::EXTENSION_POPUP_VIEW_READY,
        Source<Profile>(profile_),
        Details<ExtensionHost>(this));
  }
}

void ExtensionHost::RunJavaScriptMessage(const std::wstring& message,
                                         const std::wstring& default_prompt,
                                         const GURL& frame_url,
                                         const int flags,
                                         IPC::Message* reply_msg,
                                         bool* did_suppress_message) {
  *did_suppress_message = false;
  // Unlike for page alerts, navigations aren't a good signal for when to
  // resume showing alerts, so we can't reasonably stop showing them even if
  // the extension is spammy.
  RunJavascriptMessageBox(this, frame_url, flags, message, default_prompt,
                          false, reply_msg);
}

std::wstring ExtensionHost::GetMessageBoxTitle(const GURL& frame_url,
                                               bool is_alert) {
  if (extension_->name().empty())
    return l10n_util::GetString(
        is_alert ? IDS_EXTENSION_ALERT_DEFAULT_TITLE
                 : IDS_EXTENSION_MESSAGEBOX_DEFAULT_TITLE);

  return l10n_util::GetStringF(
      is_alert ? IDS_EXTENSION_ALERT_TITLE : IDS_EXTENSION_MESSAGEBOX_TITLE,
      UTF8ToWide(extension_->name()));
}

gfx::NativeWindow ExtensionHost::GetMessageBoxRootWindow() {
  // If we have a view, use that.
  gfx::NativeView native_view = GetNativeViewOfHost();
  if (native_view)
    return platform_util::GetTopLevel(native_view);

  // Otherwise, try the active tab's view.
  Browser* browser = extension_function_dispatcher_->GetCurrentBrowser(true);
  if (browser) {
    TabContents* active_tab = browser->GetSelectedTabContents();
    if (active_tab)
      return active_tab->view()->GetTopLevelNativeWindow();
  }

  return NULL;
}

void ExtensionHost::OnMessageBoxClosed(IPC::Message* reply_msg,
                                       bool success,
                                       const std::wstring& prompt) {
  render_view_host()->JavaScriptMessageBoxClosed(reply_msg, success, prompt);
}

void ExtensionHost::Close(RenderViewHost* render_view_host) {
  if (extension_host_type_ == ViewType::EXTENSION_POPUP ||
      extension_host_type_ == ViewType::EXTENSION_INFOBAR) {
    NotificationService::current()->Notify(
        NotificationType::EXTENSION_HOST_VIEW_SHOULD_CLOSE,
        Source<Profile>(profile_),
        Details<ExtensionHost>(this));
  }
}

RendererPreferences ExtensionHost::GetRendererPrefs(Profile* profile) const {
  RendererPreferences preferences;
  renderer_preferences_util::UpdateFromSystemSettings(&preferences, profile);
  return preferences;
}

WebPreferences ExtensionHost::GetWebkitPrefs() {
  Profile* profile = render_view_host()->process()->profile();
  WebPreferences webkit_prefs =
      RenderViewHostDelegateHelper::GetWebkitPrefs(profile,
                                                   false);  // is_dom_ui
  // Extensions are trusted so we override any user preferences for disabling
  // javascript or images.
  webkit_prefs.loads_images_automatically = true;
  webkit_prefs.javascript_enabled = true;

  if (extension_host_type_ == ViewType::EXTENSION_POPUP ||
      extension_host_type_ == ViewType::EXTENSION_INFOBAR)
    webkit_prefs.allow_scripts_to_close_windows = true;

  // TODO(dcheng): incorporate this setting into kClipboardPermission check.
  webkit_prefs.javascript_can_access_clipboard = true;

  // TODO(dcheng): check kClipboardPermission instead once it's implemented.
  if (extension_->HasApiPermission(Extension::kExperimentalPermission))
    webkit_prefs.dom_paste_enabled = true;
  return webkit_prefs;
}

void ExtensionHost::ProcessDOMUIMessage(const std::string& message,
                                        const ListValue* content,
                                        const GURL& source_url,
                                        int request_id,
                                        bool has_callback) {
  if (extension_function_dispatcher_.get()) {
    extension_function_dispatcher_->HandleRequest(
        message, content, source_url, request_id, has_callback);
  }
}

RenderViewHostDelegate::View* ExtensionHost::GetViewDelegate() {
  return this;
}

void ExtensionHost::CreateNewWindow(
    int route_id,
    WindowContainerType window_container_type,
    const string16& frame_name) {
  delegate_view_helper_.CreateNewWindow(
      route_id,
      render_view_host()->process()->profile(),
      site_instance(),
      DOMUIFactory::GetDOMUIType(url_),
      this,
      window_container_type,
      frame_name);
}

void ExtensionHost::CreateNewWidget(int route_id,
                                    WebKit::WebPopupType popup_type) {
  CreateNewWidgetInternal(route_id, popup_type);
}

RenderWidgetHostView* ExtensionHost::CreateNewWidgetInternal(
    int route_id, WebKit::WebPopupType popup_type) {
  return delegate_view_helper_.CreateNewWidget(route_id, popup_type,
                                               site_instance()->GetProcess());
}

void ExtensionHost::ShowCreatedWindow(int route_id,
                                      WindowOpenDisposition disposition,
                                      const gfx::Rect& initial_pos,
                                      bool user_gesture) {
  TabContents* contents = delegate_view_helper_.GetCreatedWindow(route_id);
  if (!contents)
    return;

  Browser* browser = extension_function_dispatcher_->GetCurrentBrowser(
      profile_->GetExtensionsService()->IsIncognitoEnabled(extension_));
  if (!browser)
    return;

  browser->AddTabContents(contents, disposition, initial_pos, user_gesture);
}

void ExtensionHost::ShowCreatedWidget(int route_id,
                                      const gfx::Rect& initial_pos) {
  ShowCreatedWidgetInternal(delegate_view_helper_.GetCreatedWidget(route_id),
                            initial_pos);
}

void ExtensionHost::ShowCreatedWidgetInternal(
    RenderWidgetHostView* widget_host_view,
    const gfx::Rect& initial_pos) {
  Browser *browser = GetBrowser();
  DCHECK(browser);
  if (!browser)
    return;
  browser->BrowserRenderWidgetShowing();
  // TODO(erikkay): These two lines could be refactored with TabContentsView.
  widget_host_view->InitAsPopup(render_view_host()->view(), initial_pos);
  widget_host_view->GetRenderWidgetHost()->Init();
}

void ExtensionHost::ShowContextMenu(const ContextMenuParams& params) {
  // TODO(erikkay) Show a default context menu.
}

void ExtensionHost::StartDragging(const WebDropData& drop_data,
    WebDragOperationsMask operation_mask,
    const SkBitmap& image,
    const gfx::Point& image_offset) {
  // We're not going to do any drag & drop, but we have to tell the renderer the
  // drag & drop ended, othewise the renderer thinks the drag operation is
  // underway and mouse events won't work.  See bug 34061.
  // TODO(twiz) Implement drag & drop support for ExtensionHost instances.
  // See feature issue 36288.
  render_view_host()->DragSourceSystemDragEnded();
}

void ExtensionHost::UpdateDragCursor(WebDragOperation operation) {
}

void ExtensionHost::GotFocus() {
#if defined(TOOLKIT_VIEWS)
  // Request focus so that the FocusManager has a focused view and can perform
  // normally its key event processing (so that it lets tab key events go to the
  // renderer).
  view()->RequestFocus();
#else
  // TODO(port)
#endif
}

void ExtensionHost::TakeFocus(bool reverse) {
}

bool ExtensionHost::PreHandleKeyboardEvent(const NativeWebKeyboardEvent& event,
                                           bool* is_keyboard_shortcut) {
  if (extension_host_type_ == ViewType::EXTENSION_POPUP &&
      event.type == NativeWebKeyboardEvent::RawKeyDown &&
      event.windowsKeyCode == base::VKEY_ESCAPE) {
    DCHECK(is_keyboard_shortcut != NULL);
    *is_keyboard_shortcut = true;
  }
  return false;
}

void ExtensionHost::HandleKeyboardEvent(const NativeWebKeyboardEvent& event) {
  if (extension_host_type_ == ViewType::EXTENSION_POPUP) {
    if (event.type == NativeWebKeyboardEvent::RawKeyDown &&
        event.windowsKeyCode == base::VKEY_ESCAPE) {
      NotificationService::current()->Notify(
          NotificationType::EXTENSION_HOST_VIEW_SHOULD_CLOSE,
          Source<Profile>(profile_),
          Details<ExtensionHost>(this));
      return;
    }
  }
  UnhandledKeyboardEvent(event);
}

void ExtensionHost::HandleMouseEvent() {
#if defined(OS_WIN)
  if (view_.get())
    view_->HandleMouseEvent();
#endif
}

void ExtensionHost::HandleMouseLeave() {
#if defined(OS_WIN)
  if (view_.get())
    view_->HandleMouseLeave();
#endif
}

void ExtensionHost::SetRenderViewType(ViewType::Type type) {
  DCHECK(type == ViewType::EXTENSION_MOLE ||
         type == ViewType::EXTENSION_TOOLSTRIP ||
         type == ViewType::EXTENSION_POPUP);
  extension_host_type_ = type;
  render_view_host()->ViewTypeChanged(extension_host_type_);
}

ViewType::Type ExtensionHost::GetRenderViewType() const {
  return extension_host_type_;
}

void ExtensionHost::RenderViewCreated(RenderViewHost* render_view_host) {
  if (view_.get())
    view_->RenderViewCreated();

  // TODO(mpcomplete): This is duplicated in DidNavigate, which means that
  // we'll create 2 EFDs for the first navigation. We should try to find a
  // better way to unify them.
  // See http://code.google.com/p/chromium/issues/detail?id=18240
  LOG(INFO) << "(RenderViewCreated) Resetting EFD to " << url_.spec() << " for "
      << extension_->name();
  extension_function_dispatcher_.reset(
      ExtensionFunctionDispatcher::Create(render_view_host, this, url_));

  if (extension_host_type_ == ViewType::EXTENSION_TOOLSTRIP ||
      extension_host_type_ == ViewType::EXTENSION_MOLE ||
      extension_host_type_ == ViewType::EXTENSION_POPUP ||
      extension_host_type_ == ViewType::EXTENSION_INFOBAR) {
    render_view_host->EnablePreferredSizeChangedMode(
        kPreferredSizeWidth | kPreferredSizeHeightThisIsSlow);
  }
}

int ExtensionHost::GetBrowserWindowID() const {
  // Hosts not attached to any browser window have an id of -1.  This includes
  // those mentioned below, and background pages.
  int window_id = extension_misc::kUnknownWindowId;
  if (extension_host_type_ == ViewType::EXTENSION_TOOLSTRIP ||
      extension_host_type_ == ViewType::EXTENSION_MOLE ||
      extension_host_type_ == ViewType::EXTENSION_POPUP ||
      extension_host_type_ == ViewType::EXTENSION_INFOBAR) {
    // If the host is bound to a browser, then extract its window id.
    // Extensions hosted in ExternalTabContainer objects may not have
    // an associated browser.
    Browser* browser = GetBrowser();
    if (browser)
      window_id = ExtensionTabUtil::GetWindowId(browser);
  } else if (extension_host_type_ != ViewType::EXTENSION_BACKGROUND_PAGE) {
    NOTREACHED();
  }
  return window_id;
}
