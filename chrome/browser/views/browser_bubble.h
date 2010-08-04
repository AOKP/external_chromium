// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_VIEWS_BROWSER_BUBBLE_H_
#define CHROME_BROWSER_VIEWS_BROWSER_BUBBLE_H_

#include "views/view.h"
#include "views/widget/widget.h"

class BrowserBubbleHost;

// A class for creating a floating window that is "attached" to a particular
// Browser.  If you don't install a delegate, the bubble will hide
// automatically when the browser moves.  The bubble is only shown manually.
// Users are expected to delete the bubble when finished with it.
// Class assumes that RTL related mirroring is done by the view.
class BrowserBubble {
 public:
  // Delegate to browser bubble events.
  class Delegate {
   public:
    // Called when the Browser Window that this bubble is attached to moves.
    virtual void BubbleBrowserWindowMoved(BrowserBubble* bubble) {}

    // Called with the Browser Window that this bubble is attached to is
    // about to close.
    virtual void BubbleBrowserWindowClosing(BrowserBubble* bubble) {}

    // Called when the bubble became active / got focus.
    virtual void BubbleGotFocus(BrowserBubble* bubble) {}

    // Called when the bubble became inactive / lost focus.
    // |lost_focus_to_child| is true when a child window became active.
    virtual void BubbleLostFocus(BrowserBubble* bubble,
                                 bool lost_focus_to_child) {}
  };

  // Note that the bubble will size itself to the preferred size of |view|.
  // |view| is the embedded view, |frame| is widget that the bubble is being
  // positioned relative to, |origin| is the location that the bubble will
  // be positioned relative to |frame|.  Pass true through |drop_shadow| to
  // surround the bubble widget with a drop-shadow.
  BrowserBubble(views::View* view, views::Widget* frame,
                const gfx::Point& origin, bool drop_shadow);
  virtual ~BrowserBubble();

  // Call manually if you need to detach the bubble from tracking the browser's
  // position.  Note that you must call this manually before deleting this
  // object since it can't be safely called from the destructor.
  void DetachFromBrowser();

  // Normally called automatically during construction, but if DetachFromBrowser
  // has been called manually, then this call will reattach.
  void AttachToBrowser();
  bool attached() const { return attached_; }

  // Get/Set the delegate.
  Delegate* delegate() const { return delegate_; }
  void set_delegate(Delegate* del) { delegate_ = del; }

  // Notifications from BrowserBubbleHost.
  // With no delegate, both of these default to Hiding the bubble.
  virtual void BrowserWindowMoved();
  virtual void BrowserWindowClosing();

  // Show or hide the bubble.
  virtual void Show(bool activate);
  virtual void Hide();
  bool visible() const { return visible_; }

  // The contained view.
  views::View* view() const { return view_; }

  // Set the bounds of the bubble relative to the browser window.
  void SetBounds(int x, int y, int w, int h);
  void MoveTo(int x, int y);
  int width() { return bounds_.width(); }
  int height() { return bounds_.height(); }
  const gfx::Rect& bounds() const { return bounds_; }

  // Reposition the bubble - as we are using a WS_POPUP for the bubble,
  // we have to manually position it when the browser window moves.
  void Reposition();

  // Resize the bubble to fit the view.
  void ResizeToView();

  // Returns the NativeView containing that popup.
  gfx::NativeView native_view() const { return popup_->GetNativeView(); }

 protected:
  // Create the popup widget.
  virtual void InitPopup();

  // Move the popup to an absolute position.
  void MovePopup(int x, int y, int w, int h);

  // The widget that this bubble is in.
  views::Widget* popup_;

  // The frame that this bubble is attached to.
  views::Widget* frame_;

 private:
  // The view that is displayed in this bubble.
  views::View* view_;

  // The bounds relative to the frame.
  gfx::Rect bounds_;

  // Current visibility.
  bool visible_;

  // The delegate isn't owned by the bubble.
  Delegate* delegate_;

  // Is the bubble attached to a Browser window.
  bool attached_;

  // Does the bubble have a drop-shadow.
  bool drop_shadow_enabled_;

  // Non-owning pointer to the host of this bubble.
  BrowserBubbleHost* bubble_host_;

  DISALLOW_COPY_AND_ASSIGN(BrowserBubble);
};

#endif  // CHROME_BROWSER_VIEWS_BROWSER_BUBBLE_H_
