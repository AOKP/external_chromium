// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/views/domui_menu_widget.h"

#include "base/stringprintf.h"
#include "base/singleton.h"
#include "base/task.h"
#include "chrome/browser/browser_thread.h"
#include "chrome/browser/chromeos/views/menu_locator.h"
#include "chrome/browser/chromeos/views/native_menu_domui.h"
#include "chrome/browser/chromeos/wm_ipc.h"
#include "chrome/browser/renderer_host/render_view_host.h"
#include "chrome/browser/renderer_host/render_widget_host_view.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/browser/views/dom_view.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/url_constants.h"
#include "cros/chromeos_wm_ipc_enums.h"
#include "gfx/canvas_skia.h"
#include "googleurl/src/gurl.h"
#include "third_party/skia/include/effects/SkGradientShader.h"
#include "views/border.h"
#include "views/layout_manager.h"
#include "views/widget/root_view.h"

namespace {

// Colors for menu's graident background.
const SkColor kMenuStartColor = SK_ColorWHITE;
const SkColor kMenuEndColor = 0xFFEEEEEE;

// Rounded border for menu. This draws three types of rounded border,
// for context menu, dropdown menu and submenu. Please see
// menu_locator.cc for details.
class RoundedBorder : public views::Border {
 public:
  explicit RoundedBorder(chromeos::MenuLocator* locator)
      : menu_locator_(locator) {
  }

 private:
  // views::Border implementatios.
  virtual void Paint(const views::View& view, gfx::Canvas* canvas) const {
    const SkScalar* corners = menu_locator_->GetCorners();
    // The menu is in off screen so no need to draw corners.
    if (!corners)
      return;
    int w = view.width();
    int h = view.height();
    SkRect rect = {0, 0, w, h};
    SkPath path;
    path.addRoundRect(rect, corners);
    SkPaint paint;
    paint.setStyle(SkPaint::kFill_Style);
    paint.setFlags(SkPaint::kAntiAlias_Flag);
    SkPoint p[2] = { {0, 0}, {0, h} };
    SkColor colors[2] = {kMenuStartColor, kMenuEndColor};
    SkShader* s = SkGradientShader::CreateLinear(
        p, colors, NULL, 2, SkShader::kClamp_TileMode, NULL);
    paint.setShader(s);
    // Need to unref shader, otherwise never deleted.
    s->unref();
    canvas->AsCanvasSkia()->drawPath(path, paint);
  }

  virtual void GetInsets(gfx::Insets* insets) const {
    DCHECK(insets);
    menu_locator_->GetInsets(insets);
  }

  chromeos::MenuLocator* menu_locator_;  // not owned

  DISALLOW_COPY_AND_ASSIGN(RoundedBorder);
};

class InsetsLayout : public views::LayoutManager {
 public:
  InsetsLayout() : views::LayoutManager() {}

 private:
  // views::LayoutManager implementatios.
  virtual void Layout(views::View* host) {
    if (host->GetChildViewCount() == 0)
      return;
    gfx::Insets insets = host->GetInsets();
    views::View* view = host->GetChildViewAt(0);

    view->SetBounds(insets.left(), insets.top(),
                    host->width() - insets.width(),
                    host->height() - insets.height());
  }

  virtual gfx::Size GetPreferredSize(views::View* host) {
    DCHECK(host->GetChildViewCount() == 1);
    gfx::Insets insets = host->GetInsets();
    gfx::Size size = host->GetChildViewAt(0)->GetPreferredSize();
    return gfx::Size(size.width() + insets.width(),
                     size.height() + insets.height());
  }

  DISALLOW_COPY_AND_ASSIGN(InsetsLayout);
};

const int kDOMViewWarmUpDelayMs = 1000 * 5;

// A delayed task to initialize a cache. This is
// create when a profile is switched.
// (incognito, oobe/login has different profile).
class WarmUpTask : public Task {
 public:
  WarmUpTask() {}
  virtual ~WarmUpTask() {}

  virtual void Run();

 private:
  DISALLOW_COPY_AND_ASSIGN(WarmUpTask);
};

// DOMViewCache holds single cache instance of DOMView.
class DOMViewCache : NotificationObserver {
 public:
  DOMViewCache()
      : current_profile_(NULL),
        cache_(NULL),
        warmup_enabled_(true) {
    registrar_.Add(this, NotificationType::APP_TERMINATING,
                   NotificationService::AllSources());
  }
  virtual ~DOMViewCache() {}

  // Returns a DOMView for given profile. If there is
  // matching cache,
  DOMView* Get(Profile* profile) {
    if (cache_ &&
        cache_->tab_contents()->profile() == profile) {
      DOMView* c = cache_;
      cache_ = NULL;
      CheckClassInvariant();
      return c;
    }
    DOMView* dom_view = new DOMView();
    dom_view->Init(profile, NULL);
    CheckClassInvariant();
    return dom_view;
  }

  // Release a dom_view. A dom view is reused if its profile matches
  // the current profile, or gets deleted otherwise.
  void Release(DOMView* dom_view) {
    if (cache_ == NULL &&
        current_profile_ == dom_view->tab_contents()->profile()) {
      cache_ = dom_view;
    } else {
      delete dom_view;
    }
    CheckClassInvariant();
  }

  // (Re)Initiailzes the cache with profile.
  // If the current profile does not match the new profile,
  // it deletes the existing cache (if any) and creates new one.
  void Init(Profile* profile) {
    if (current_profile_ != profile) {
      delete cache_;
      cache_ = NULL;
      current_profile_ = profile;
      BrowserThread::PostDelayedTask(BrowserThread::UI,
                                     FROM_HERE,
                                     new WarmUpTask(),
                                     kDOMViewWarmUpDelayMs);
    }
    CheckClassInvariant();
  }

  // Create a cache if one does not exist yet.
  void WarmUp() {
    // skip if domui is created in delay, or
    // chromeos is shutting down.
    if (cache_ || !current_profile_ || !warmup_enabled_) {
      CheckClassInvariant();
      return;
    }
    cache_ = new DOMView();
    cache_->Init(current_profile_, NULL);
    /**
     * Tentative workaround for the failing tests that expects
     * page loads.
    cache_->LoadURL(
        GURL(StringPrintf("chrome://%s", chrome::kChromeUIMenu)));
     */
    CheckClassInvariant();
  }

  // Deletes cached DOMView instance if any.
  void Shutdown() {
    delete cache_;
    cache_ = NULL;
    // Reset current_profile_ as well so that a domview that
    // is currently in use will be deleted in Release as well.
    current_profile_ = NULL;
  }

  // Enable/disable warmup. This has to be called
  // before WarmUp method is invoked.
  void set_warmup_enabled(bool enabled) {
    warmup_enabled_ = enabled;
  }

 private:
  // NotificationObserver impelmentation:
  void Observe(NotificationType type,
               const NotificationSource& source,
               const NotificationDetails& details) {
    DCHECK_EQ(NotificationType::APP_TERMINATING, type.value);
    Shutdown();
  }

  // Tests the class invariant condition.
  void CheckClassInvariant() {
    DCHECK(!cache_ ||
           cache_->tab_contents()->profile() == current_profile_);
  }

  Profile* current_profile_;
  DOMView* cache_;
  NotificationRegistrar registrar_;
  bool warmup_enabled_;
};

void WarmUpTask::Run() {
  Singleton<DOMViewCache>::get()->WarmUp();
}

// A gtk widget key used to test if a given WidgetGtk instance is
// DOMUIMenuWidgetKey.
const char* kDOMUIMenuWidgetKey = "__DOMUI_MENU_WIDGET__";

}  // namespace

namespace chromeos {

// static
DOMUIMenuWidget* DOMUIMenuWidget::FindDOMUIMenuWidget(gfx::NativeView native) {
  DCHECK(native);
  native = gtk_widget_get_toplevel(native);
  if (!native)
    return NULL;
  return static_cast<chromeos::DOMUIMenuWidget*>(
      g_object_get_data(G_OBJECT(native), kDOMUIMenuWidgetKey));
}

///////////////////////////////////////////////////////////////////////////////
// DOMUIMenuWidget public:

DOMUIMenuWidget::DOMUIMenuWidget(chromeos::NativeMenuDOMUI* domui_menu,
                                 bool root)
    : views::WidgetGtk(views::WidgetGtk::TYPE_POPUP),
      domui_menu_(domui_menu),
      dom_view_(NULL),
      did_pointer_grab_(false),
      is_root_(root) {
  DCHECK(domui_menu_);
  // TODO(oshima): Disabling transparent until we migrate bookmark
  // menus to DOMUI.  See crosbug.com/7718.
  // MakeTransparent();

  Singleton<DOMViewCache>::get()->Init(domui_menu->GetProfile());
}

DOMUIMenuWidget::~DOMUIMenuWidget() {
}

void DOMUIMenuWidget::Init(gfx::NativeView parent, const gfx::Rect& bounds) {
  WidgetGtk::Init(parent, bounds);
  gtk_window_set_destroy_with_parent(GTK_WINDOW(GetNativeView()), TRUE);
  gtk_window_set_type_hint(GTK_WINDOW(GetNativeView()),
                           GDK_WINDOW_TYPE_HINT_MENU);
  g_object_set_data(G_OBJECT(GetNativeView()), kDOMUIMenuWidgetKey, this);
}

void DOMUIMenuWidget::Hide() {
  ReleaseGrab();
  WidgetGtk::Hide();
  // Clears the content.
  ExecuteJavascript(L"updateModel({'items':[]})");
}

void DOMUIMenuWidget::Close() {
  if (dom_view_ != NULL) {
    dom_view_->GetParent()->RemoveChildView(dom_view_);
    Singleton<DOMViewCache>::get()->Release(dom_view_);
    dom_view_ = NULL;
  }

  // Detach the domui_menu_ which is being deleted.
  domui_menu_ = NULL;
  views::WidgetGtk::Close();
}

void DOMUIMenuWidget::ReleaseGrab() {
  WidgetGtk::ReleaseGrab();
  if (did_pointer_grab_) {
    did_pointer_grab_ = false;
    gdk_pointer_ungrab(GDK_CURRENT_TIME);

    ClearGrabWidget();
  }
}

gboolean DOMUIMenuWidget::OnGrabBrokeEvent(GtkWidget* widget,
                                           GdkEvent* event) {
  did_pointer_grab_ = false;
  Hide();
  return WidgetGtk::OnGrabBrokeEvent(widget, event);
}

void DOMUIMenuWidget::OnSizeAllocate(GtkWidget* widget,
                                     GtkAllocation* allocation) {
  views::WidgetGtk::OnSizeAllocate(widget, allocation);
  // Adjust location when menu gets resized.
  gfx::Rect bounds;
  GetBounds(&bounds, false);
  // Don't move until the menu gets contents.
  if (bounds.height() > 1) {
    menu_locator_->Move(this);
    domui_menu_->InputIsReady();
  }
}

gboolean MapToFocus(GtkWidget* widget, GdkEvent* event, gpointer data) {
  DOMUIMenuWidget* menu_widget = DOMUIMenuWidget::FindDOMUIMenuWidget(widget);
  if (menu_widget) {
    // See EnableInput for the meaning of data.
    bool select_item = data != NULL;
    menu_widget->EnableInput(select_item);
  }
  return true;
}

void DOMUIMenuWidget::EnableScroll(bool enable) {
  ExecuteJavascript(StringPrintf(
      L"enableScroll(%ls)", enable ? L"true" : L"false" ));
}

void DOMUIMenuWidget::EnableInput(bool select_item) {
  if (!dom_view_)
    return;
  DCHECK(dom_view_->tab_contents()->render_view_host());
  DCHECK(dom_view_->tab_contents()->render_view_host()->view());
  GtkWidget* target =
      dom_view_->tab_contents()->render_view_host()->view()->GetNativeView();
  DCHECK(target);
  // Skip if the widget already own the input.
  if (gtk_grab_get_current() == target)
    return;

  ClearGrabWidget();

  if (!GTK_WIDGET_REALIZED(target)) {
    // Wait grabbing widget if the widget is not yet realized.
    // Using data as a flag. |select_item| is false if data is NULL,
    // or true otherwise.
    g_signal_connect(G_OBJECT(target), "map-event",
                     G_CALLBACK(&MapToFocus),
                     select_item ? this : NULL);
    return;
  }

  gtk_grab_add(target);
  dom_view_->tab_contents()->Focus();
  if (select_item) {
    ExecuteJavascript(L"selectItem()");
  }
}

void DOMUIMenuWidget::ExecuteJavascript(const std::wstring& script) {
  // Don't exeute there is no DOMView associated. This is fine because
  // 1) selectItem make sense only when DOMView is associated.
  // 2) updateModel will be called again when a DOMView is created/assigned.
  if (!dom_view_)
    return;

  DCHECK(dom_view_->tab_contents()->render_view_host());
  dom_view_->tab_contents()->render_view_host()->
      ExecuteJavascriptInWebFrame(std::wstring(), script);
}

void DOMUIMenuWidget::ShowAt(chromeos::MenuLocator* locator) {
  DCHECK(domui_menu_);
  menu_locator_.reset(locator);
  if (!dom_view_) {
    dom_view_ = Singleton<DOMViewCache>::get()->Get(domui_menu_->GetProfile());
    dom_view_->Init(domui_menu_->GetProfile(), NULL);
    // TODO(oshima): remove extra view to draw rounded corner.
    views::View* container = new views::View();
    container->AddChildView(dom_view_);
    container->set_border(new RoundedBorder(locator));
    container->SetLayoutManager(new InsetsLayout());
    SetContentsView(container);
    dom_view_->LoadURL(domui_menu_->menu_url());
  } else {
    domui_menu_->UpdateStates();
    dom_view_->GetParent()->set_border(new RoundedBorder(locator));
    menu_locator_->Move(this);
  }
  Show();

  // The pointer grab is captured only on the top level menu,
  // all mouse event events are delivered to submenu using gtk_add_grab.
  if (is_root_) {
    CaptureGrab();
  }
}

void DOMUIMenuWidget::SetSize(const gfx::Size& new_size) {
  DCHECK(domui_menu_);
  // Ignore the empty new_size request which is called when
  // menu.html is loaded.
  if (new_size.IsEmpty())
    return;
  int width, height;
  gtk_widget_get_size_request(GetNativeView(), &width, &height);
  gfx::Size real_size(std::max(new_size.width(), width),
                      new_size.height());
  // Ignore the size request with the same size.
  gfx::Rect bounds;
  GetBounds(&bounds, false);
  if (bounds.size() == real_size)
    return;
  menu_locator_->SetBounds(this, real_size);
}

///////////////////////////////////////////////////////////////////////////////
// DOMUIMenuWidget private:

void DOMUIMenuWidget::CaptureGrab() {
  // Release the current grab.
  ClearGrabWidget();

  // NOTE: we do this to ensure we get mouse events from other apps, a grab
  // done with gtk_grab_add doesn't get events from other apps.
  GdkGrabStatus grab_status =
      gdk_pointer_grab(window_contents()->window, FALSE,
                       static_cast<GdkEventMask>(
                           GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                           GDK_POINTER_MOTION_MASK),
                       NULL, NULL, GDK_CURRENT_TIME);
  did_pointer_grab_ = (grab_status == GDK_GRAB_SUCCESS);
  DCHECK(did_pointer_grab_);

  EnableInput(false /* no selection */);
}

void DOMUIMenuWidget::ClearGrabWidget() {
  GtkWidget* grab_widget;
  while ((grab_widget = gtk_grab_get_current()))
    gtk_grab_remove(grab_widget);
}

void DOMUIMenuWidget::DisableWarmUp() {
  Singleton<DOMViewCache>::get()->set_warmup_enabled(false);
}

}   // namespace chromeos
