// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_GTK_TAB_CONTENTS_CONTAINER_GTK_H_
#define CHROME_BROWSER_GTK_TAB_CONTENTS_CONTAINER_GTK_H_
#pragma once

#include <gtk/gtk.h>

#include "app/gtk_signal.h"
#include "base/basictypes.h"
#include "chrome/browser/gtk/owned_widget_gtk.h"
#include "chrome/browser/gtk/view_id_util.h"
#include "chrome/common/notification_observer.h"
#include "chrome/common/notification_registrar.h"

class RenderViewHost;
class StatusBubbleGtk;
class TabContents;

typedef struct _GtkFloatingContainer GtkFloatingContainer;

class TabContentsContainerGtk : public NotificationObserver,
                                public ViewIDUtil::Delegate {
 public:
  explicit TabContentsContainerGtk(StatusBubbleGtk* status_bubble);
  ~TabContentsContainerGtk();

  void Init();

  // Make the specified tab visible.
  void SetTabContents(TabContents* tab_contents);
  TabContents* GetTabContents() const { return tab_contents_; }

  // Gets the tab contents currently being displayed (either |tab_contents_| or
  // |preview_contents_|).
  TabContents* GetVisibleTabContents();

  void SetPreviewContents(TabContents* preview);
  void PopPreviewContents();

  // Remove the tab from the hierarchy.
  void DetachTabContents(TabContents* tab_contents);

  // NotificationObserver implementation.
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

  GtkWidget* widget() { return floating_.get(); }

  // ViewIDUtil::Delegate implementation ---------------------------------------
  virtual GtkWidget* GetWidgetForViewID(ViewID id);

 private:
  // Called when a TabContents is destroyed. This gives us a chance to clean
  // up our internal state if the TabContents is somehow destroyed before we
  // get notified.
  void TabContentsDestroyed(TabContents* contents);

  // Handler for |floating_|'s "set-floating-position" signal. During this
  // callback, we manually set the position of the status bubble.
  static void OnSetFloatingPosition(
      GtkFloatingContainer* container, GtkAllocation* allocation,
      TabContentsContainerGtk* tab_contents_container);

  // Add |contents| to the container and start showing it.
  void PackTabContents(TabContents* contents);

  // Stop showing |contents|.
  void HideTabContents(TabContents* contents);

  // Removes |preview_contents_|.
  void RemovePreviewContents();

  // Handle focus traversal on the tab contents container. Focus should not
  // traverse to the preview contents.
  CHROMEGTK_CALLBACK_1(TabContentsContainerGtk, gboolean, OnFocus,
                       GtkDirectionType);

  NotificationRegistrar registrar_;

  // The TabContents for the currently selected tab. This will be showing unless
  // there is a preview contents.
  TabContents* tab_contents_;

  // The current preview contents (for instant). If non-NULL, it will be
  // visible.
  TabContents* preview_contents_;

  // The status bubble manager.  Always non-NULL.
  StatusBubbleGtk* status_bubble_;

  // Top of the TabContentsContainerGtk widget hierarchy. A cross between a
  // GtkBin and a GtkFixed, |floating_| has |expanded_| as its one "real" child,
  // and the various things that hang off the bottom (status bubble, etc) have
  // their positions manually set in OnSetFloatingPosition.
  OwnedWidgetGtk floating_;

  // We insert and remove TabContents GtkWidgets into this expanded_. This
  // should not be a GtkVBox since there were errors with timing where the vbox
  // was horizontally split with the top half displaying the current TabContents
  // and bottom half displaying the loading page.
  GtkWidget* expanded_;

  DISALLOW_COPY_AND_ASSIGN(TabContentsContainerGtk);
};

#endif  // CHROME_BROWSER_GTK_TAB_CONTENTS_CONTAINER_GTK_H_
