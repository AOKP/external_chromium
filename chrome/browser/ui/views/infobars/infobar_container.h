// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_INFOBARS_INFOBAR_CONTAINER_H_
#define CHROME_BROWSER_UI_VIEWS_INFOBARS_INFOBAR_CONTAINER_H_
#pragma once

#include <set>

#include "chrome/browser/ui/views/accessible_pane_view.h"
#include "chrome/common/notification_observer.h"
#include "chrome/common/notification_registrar.h"
#include "views/view.h"

class BrowserView;
class InfoBar;
class InfoBarDelegate;
class InfoBarView;
class TabContents;

// A views::View subclass that contains a collection of InfoBars associated with
// a TabContents.
class InfoBarContainer : public AccessiblePaneView,
                         public NotificationObserver {
 public:
  // The delegate is notified each time InfoBarContainer::OnInfoBarAnimated() is
  // called.
  class Delegate {
   public:
    virtual void InfoBarContainerSizeChanged(bool is_animating) = 0;

   protected:
    virtual ~Delegate();
  };

  explicit InfoBarContainer(Delegate* delegate);
  virtual ~InfoBarContainer();

  // Changes the TabContents for which this container is showing infobars.  This
  // will remove all current infobars from the container, add the infobars from
  // |contents|, and show them all.  |contents| may be NULL.
  void ChangeTabContents(TabContents* contents);

  // Called when a contained infobar has animated.  The container is expected to
  // do anything necessary to respond to the infobar's possible size change,
  // e.g. re-layout.
  void OnInfoBarAnimated(bool done);

  // Remove the specified InfoBarDelegate from the selected TabContents. This
  // will notify us back and cause us to close the InfoBar.  This is called from
  // the InfoBar's close button handler.
  void RemoveDelegate(InfoBarDelegate* delegate);

  // Called by |infobar| to request that it be removed from the container, as it
  // is about to delete itself.  At this point, |infobar| should already be
  // hidden.
  void RemoveInfoBar(InfoBarView* infobar);

  // Paint the InfoBar arrows on |canvas|. |arrow_center_x| indicates
  // the desired location of the center of the arrow in the
  // |outer_view| coordinate system.
  void PaintInfoBarArrows(gfx::Canvas* canvas,
                          View* outer_view,
                          int arrow_center_x);

 private:
  typedef std::set<InfoBarView*> InfoBars;

  // AccessiblePaneView:
  virtual gfx::Size GetPreferredSize();
  virtual void Layout();
  virtual AccessibilityTypes::Role GetAccessibleRole();

  // NotificationObserver:
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

  // Removes an InfoBar for the specified delegate, in response to a
  // notification from the selected TabContents. The InfoBar's disappearance
  // will be animated if |use_animation| is true.
  void RemoveInfoBar(InfoBarDelegate* delegate, bool use_animation);

  // Adds |infobar| to this container and calls Show() on it.  |animate| is
  // passed along to infobar->Show().  Depending on the value of
  // |callback_status|, this calls infobar->set_container(this) either before or
  // after the call to Show() so that OnInfoBarAnimated() either will or won't
  // be called as a result.
  enum CallbackStatus { NO_CALLBACK, WANT_CALLBACK };
  void AddInfoBar(InfoBar* infobar,
                  bool animate,
                  CallbackStatus callback_status);

  NotificationRegistrar registrar_;
  Delegate* delegate_;
  TabContents* tab_contents_;
  InfoBars infobars_;

  DISALLOW_COPY_AND_ASSIGN(InfoBarContainer);
};

#endif  // CHROME_BROWSER_UI_VIEWS_INFOBARS_INFOBAR_CONTAINER_H_
