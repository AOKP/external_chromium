// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_RENDERER_HOST_RENDER_WIDGET_HOST_VIEW_H_
#define CHROME_BROWSER_RENDERER_HOST_RENDER_WIDGET_HOST_VIEW_H_

#if defined(OS_MACOSX)
#include <OpenGL/OpenGL.h>
#endif

#include "app/surface/transport_dib.h"
#include "base/shared_memory.h"
#include "gfx/native_widget_types.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/WebKit/WebKit/chromium/public/WebPopupType.h"
#include "third_party/WebKit/WebKit/chromium/public/WebTextInputType.h"
#include "webkit/glue/plugins/webplugin.h"
#include "webkit/glue/webaccessibility.h"

namespace gfx {
class Rect;
class Size;
}
namespace IPC {
class Message;
}

class BackingStore;
class RenderProcessHost;
class RenderWidgetHost;
class VideoLayer;
class WebCursor;
struct WebMenuItem;

// RenderWidgetHostView is an interface implemented by an object that acts as
// the "View" portion of a RenderWidgetHost. The RenderWidgetHost and its
// associated RenderProcessHost own the "Model" in this case which is the
// child renderer process. The View is responsible for receiving events from
// the surrounding environment and passing them to the RenderWidgetHost, and
// for actually displaying the content of the RenderWidgetHost when it
// changes.
class RenderWidgetHostView {
 public:
  virtual ~RenderWidgetHostView() {}

  // Platform-specific creator. Use this to construct new RenderWidgetHostViews
  // rather than using RenderWidgetHostViewWin & friends.
  //
  // This function must NOT size it, because the RenderView in the renderer
  // wounldn't have been created yet. The widget would set its "waiting for
  // resize ack" flag, and the ack would never come becasue no RenderView
  // received it.
  //
  // The RenderWidgetHost must already be created (because we can't know if it's
  // going to be a regular RenderWidgetHost or a RenderViewHost (a subclass).
  static RenderWidgetHostView* CreateViewForWidget(RenderWidgetHost* widget);

  // Retrieves the RenderWidgetHostView corresponding to the specified
  // |native_view|, or NULL if there is no such instance.
  static RenderWidgetHostView* GetRenderWidgetHostViewFromNativeView(
      gfx::NativeView native_view);

  // Perform all the initialization steps necessary for this object to represent
  // a popup (such as a <select> dropdown), then shows the popup at |pos|.
  virtual void InitAsPopup(RenderWidgetHostView* parent_host_view,
                           const gfx::Rect& pos) = 0;

  // Returns the associated RenderWidgetHost.
  virtual RenderWidgetHost* GetRenderWidgetHost() const = 0;

  // Notifies the View that it has become visible.
  virtual void DidBecomeSelected() = 0;

  // Notifies the View that it has been hidden.
  virtual void WasHidden() = 0;

  // Tells the View to size itself to the specified size.
  virtual void SetSize(const gfx::Size& size) = 0;

  // Retrieves the native view used to contain plugins and identify the
  // renderer in IPC messages.
  virtual gfx::NativeView GetNativeView() = 0;

  // Moves all plugin windows as described in the given list.
  virtual void MovePluginWindows(
      const std::vector<webkit_glue::WebPluginGeometry>& moves) = 0;

  // Actually set/take focus to/from the associated View component.
  virtual void Focus() = 0;
  virtual void Blur() = 0;

  // Returns true if the View currently has the focus.
  virtual bool HasFocus() = 0;

  // Shows/hides the view.  These must always be called together in pairs.
  // It is not legal to call Hide() multiple times in a row.
  virtual void Show() = 0;
  virtual void Hide() = 0;

  // Whether the view is showing.
  virtual bool IsShowing() = 0;

  // Retrieve the bounds of the View, in screen coordinates.
  virtual gfx::Rect GetViewBounds() const = 0;

  // Sets the cursor to the one associated with the specified cursor_type
  virtual void UpdateCursor(const WebCursor& cursor) = 0;

  // Indicates whether the page has finished loading.
  virtual void SetIsLoading(bool is_loading) = 0;

  // Updates the state of the input method attached to the view.
  virtual void ImeUpdateTextInputState(WebKit::WebTextInputType type,
                                       const gfx::Rect& caret_rect) = 0;

  // Cancel the ongoing composition of the input method attached to the view.
  virtual void ImeCancelComposition() = 0;

  // Informs the view that a portion of the widget's backing store was scrolled
  // and/or painted.  The view should ensure this gets copied to the screen.
  //
  // If the scroll_rect is non-empty, then a portion of the widget's backing
  // store was scrolled by dx pixels horizontally and dy pixels vertically.
  // The exposed rect from the scroll operation is included in copy_rects.
  //
  // There are subtle performance implications here.  The RenderWidget gets sent
  // a paint ack after this returns, so if the view only ever invalidates in
  // response to this, then on Windows, where WM_PAINT has lower priority than
  // events which can cause renderer resizes/paint rect updates, e.g.
  // drag-resizing can starve painting; this function thus provides the view its
  // main chance to ensure it stays painted and not just invalidated.  On the
  // other hand, if this always blindly paints, then if we're already in the
  // midst of a paint on the callstack, we can double-paint unnecessarily.
  // (Worse, we might recursively call RenderWidgetHost::GetBackingStore().)
  // Thus implementers should generally paint as much of |rect| as possible
  // synchronously with as little overpainting as possible.
  virtual void DidUpdateBackingStore(
      const gfx::Rect& scroll_rect, int scroll_dx, int scroll_dy,
      const std::vector<gfx::Rect>& copy_rects) = 0;

  // Notifies the View that the renderer has ceased to exist.
  virtual void RenderViewGone() = 0;

  // Notifies the View that the renderer will be delete soon.
  virtual void WillDestroyRenderWidget(RenderWidgetHost* rwh) = 0;

  // Tells the View to destroy itself.
  virtual void Destroy() = 0;

  // Tells the View that the tooltip text for the current mouse position over
  // the page has changed.
  virtual void SetTooltipText(const std::wstring& tooltip_text) = 0;

  // Notifies the View that the renderer text selection has changed.
  virtual void SelectionChanged(const std::string& text) {}

  // Tells the View whether the context menu is showing. This is used on Linux
  // to suppress updates to webkit focus for the duration of the show.
  virtual void ShowingContextMenu(bool showing) {}

  // Allocate a backing store for this view
  virtual BackingStore* AllocBackingStore(const gfx::Size& size) = 0;

  // Allocate a video layer for this view.
  virtual VideoLayer* AllocVideoLayer(const gfx::Size& size) = 0;

#if defined(OS_MACOSX)
  // Display a native control popup menu for WebKit.
  virtual void ShowPopupWithItems(gfx::Rect bounds,
                                  int item_height,
                                  double item_font_size,
                                  int selected_item,
                                  const std::vector<WebMenuItem>& items,
                                  bool right_aligned) = 0;

  // Get the view's position on the screen.
  virtual gfx::Rect GetWindowRect() = 0;

  // Get the view's window's position on the screen.
  virtual gfx::Rect GetRootWindowRect() = 0;

  // Set the view's active state (i.e., tint state of controls).
  virtual void SetActive(bool active) = 0;

  // Notifies the view that its enclosing window has changed visibility
  // (minimized/unminimized, app hidden/unhidden, etc).
  // TODO(stuartmorgan): This is a temporary plugin-specific workaround for
  // <http://crbug.com/34266>. Once that is fixed, this (and the corresponding
  // message and renderer-side handling) can be removed in favor of using
  // WasHidden/DidBecomeSelected.
  virtual void SetWindowVisibility(bool visible) = 0;

  // Informs the view that its containing window's frame changed.
  virtual void WindowFrameChanged() = 0;

  // Methods associated with GPU-accelerated plug-in instances.
  virtual gfx::PluginWindowHandle AllocateFakePluginWindowHandle(
      bool opaque) = 0;
  virtual void DestroyFakePluginWindowHandle(
      gfx::PluginWindowHandle window) = 0;
  virtual void AcceleratedSurfaceSetIOSurface(
      gfx::PluginWindowHandle window,
      int32 width,
      int32 height,
      uint64 io_surface_identifier) = 0;
  virtual void AcceleratedSurfaceSetTransportDIB(
      gfx::PluginWindowHandle window,
      int32 width,
      int32 height,
      TransportDIB::Handle transport_dib) = 0;
  virtual void AcceleratedSurfaceBuffersSwapped(
      gfx::PluginWindowHandle window) = 0;
  // Draws the current GPU-accelerated plug-in instances into the given context.
  virtual void DrawAcceleratedSurfaceInstances(CGLContextObj context) = 0;
#endif

#if defined(OS_LINUX)
  virtual void CreatePluginContainer(gfx::PluginWindowHandle id) = 0;
  virtual void DestroyPluginContainer(gfx::PluginWindowHandle id) = 0;
#endif

  // Toggles visual muting of the render view area. This is on when a
  // constrained window is showing.
  virtual void SetVisuallyDeemphasized(bool deemphasized) = 0;

  void set_popup_type(WebKit::WebPopupType popup_type) {
    popup_type_ = popup_type;
  }
  WebKit::WebPopupType popup_type() const { return popup_type_; }

  // Subclasses should override this method to do whatever is appropriate to set
  // the custom background for their platform.
  virtual void SetBackground(const SkBitmap& background) {
    background_ = background;
  }
  const SkBitmap& background() const { return background_; }

  // Returns true if the native view, |native_view|, is contained within in the
  // widget associated with this RenderWidgetHostView.
  virtual bool ContainsNativeView(gfx::NativeView native_view) const = 0;

  virtual void UpdateAccessibilityTree(
      const webkit_glue::WebAccessibility& tree) { }
  virtual void OnAccessibilityFocusChange(int acc_obj_id) { }
  virtual void OnAccessibilityObjectStateChange(int acc_obj_id) { }

 protected:
  // Interface class only, do not construct.
  RenderWidgetHostView() : popup_type_(WebKit::WebPopupTypeNone) {}

  // Whether this view is a popup and what kind of popup it is (select,
  // autofill...).
  WebKit::WebPopupType popup_type_;

  // A custom background to paint behind the web content. This will be tiled
  // horizontally. Can be null, in which case we fall back to painting white.
  SkBitmap background_;

 private:
  DISALLOW_COPY_AND_ASSIGN(RenderWidgetHostView);
};

#endif  // CHROME_BROWSER_RENDERER_HOST_RENDER_WIDGET_HOST_VIEW_H_
