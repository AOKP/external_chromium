// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_NOTIFICATIONS_NOTIFICATION_UI_MANAGER_H_
#define CHROME_BROWSER_NOTIFICATIONS_NOTIFICATION_UI_MANAGER_H_
#pragma once

#include <deque>
#include <string>

#include "base/id_map.h"
#include "base/scoped_ptr.h"
#include "chrome/browser/notifications/balloon.h"
#include "chrome/browser/notifications/balloon_collection.h"
#include "chrome/common/notification_observer.h"
#include "chrome/common/notification_registrar.h"

class Notification;
class Profile;
class QueuedNotification;
class SiteInstance;

// The notification manager manages use of the desktop for notifications.
// It maintains a queue of pending notifications when space becomes constrained.
class NotificationUIManager
    : public BalloonCollection::BalloonSpaceChangeListener,
      public NotificationObserver {
 public:
  NotificationUIManager();
  virtual ~NotificationUIManager();

  // Creates an initialized UI manager with a new balloon collection
  // and the listener relationship setup.
  // Except for unit tests, this is the way to construct the object.
  static NotificationUIManager* Create();

  // Initializes the UI manager with a balloon collection; this object
  // takes ownership of the balloon collection.
  void Initialize(BalloonCollection* balloon_collection) {
    DCHECK(!balloon_collection_.get());
    DCHECK(balloon_collection);
    balloon_collection_.reset(balloon_collection);
  }

  // Adds a notification to be displayed. Virtual for unit test override.
  virtual void Add(const Notification& notification,
                   Profile* profile);

  // Removes any notifications matching the supplied ID, either currently
  // displayed or in the queue.  Returns true if anything was removed.
  virtual bool CancelById(const std::string& notification_id);

  // Removes any notifications matching the supplied source origin
  // (which could be an extension ID), either currently displayed or in the
  // queue.  Returns true if anything was removed.
  virtual bool CancelAllBySourceOrigin(const GURL& source_origin);

  // Cancels all pending notifications and closes anything currently showing.
  // Used when the app is terminating.
  void CancelAll();

  // Returns balloon collection.
  BalloonCollection* balloon_collection() {
    return balloon_collection_.get();
  }

  // NotificationObserver interface (the event signaling kind of notifications)
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

 private:
  // Attempts to display notifications from the show_queue if the user
  // is active.
  void CheckAndShowNotifications();

  // Attempts to display notifications from the show_queue.
  void ShowNotifications();

  // BalloonCollectionObserver implementation.
  virtual void OnBalloonSpaceChanged();

  // Replace an existing notification with this one if applicable;
  // returns true if the replacement happened.
  bool TryReplacement(const Notification& notification);

  // An owned pointer to the collection of active balloons.
  scoped_ptr<BalloonCollection> balloon_collection_;

  // A queue of notifications which are waiting to be shown.
  typedef std::deque<QueuedNotification*> NotificationDeque;
  NotificationDeque show_queue_;

  // Registrar for the other kind of notifications (event signaling).
  NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(NotificationUIManager);
};

#endif  // CHROME_BROWSER_NOTIFICATIONS_NOTIFICATION_UI_MANAGER_H_
