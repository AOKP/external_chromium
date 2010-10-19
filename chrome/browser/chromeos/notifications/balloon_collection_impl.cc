// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/notifications/balloon_collection_impl.h"

#include <algorithm>

#include "base/logging.h"
#include "base/stl_util-inl.h"
#include "chrome/browser/browser_list.h"
#include "chrome/browser/chromeos/notifications/balloon_view.h"
#include "chrome/browser/chromeos/notifications/notification_panel.h"
#include "chrome/browser/notifications/balloon.h"
#include "chrome/browser/notifications/notification.h"
#include "chrome/browser/window_sizer.h"
#include "chrome/common/notification_service.h"
#include "gfx/rect.h"
#include "gfx/size.h"

namespace {

// Margin from the edge of the work area
const int kVerticalEdgeMargin = 5;
const int kHorizontalEdgeMargin = 5;

class NotificationMatcher {
 public:
  explicit NotificationMatcher(const Notification& notification)
      : notification_(notification) {}
  bool operator()(const Balloon* b) const {
    return notification_.IsSame(b->notification());
  }
 private:
  Notification notification_;
};

}  // namespace

namespace chromeos {

BalloonCollectionImpl::BalloonCollectionImpl()
    : notification_ui_(new NotificationPanel()) {
  registrar_.Add(this, NotificationType::BROWSER_CLOSED,
                 NotificationService::AllSources());
}

BalloonCollectionImpl::~BalloonCollectionImpl() {
  Shutdown();
}

void BalloonCollectionImpl::Add(const Notification& notification,
                                Profile* profile) {
  Balloon* new_balloon = MakeBalloon(notification, profile);
  balloons_.push_back(new_balloon);
  new_balloon->Show();
  notification_ui_->Add(new_balloon);

  // There may be no listener in a unit test.
  if (space_change_listener_)
    space_change_listener_->OnBalloonSpaceChanged();
}

bool BalloonCollectionImpl::AddDOMUIMessageCallback(
    const Notification& notification,
    const std::string& message,
    MessageCallback* callback) {
  Balloons::iterator iter = FindBalloon(notification);
  if (iter == balloons_.end()) {
    delete callback;
    return false;
  }
  BalloonViewHost* host =
      static_cast<BalloonViewHost*>((*iter)->view()->GetHost());
  return host->AddDOMUIMessageCallback(message, callback);
}

void BalloonCollectionImpl::AddSystemNotification(
    const Notification& notification,
    Profile* profile,
    bool sticky,
    bool control) {
  Balloon* new_balloon = new Balloon(notification, profile, this);
  new_balloon->set_view(
      new chromeos::BalloonViewImpl(sticky, control, true));
  balloons_.push_back(new_balloon);
  new_balloon->Show();
  notification_ui_->Add(new_balloon);

  // There may be no listener in a unit test.
  if (space_change_listener_)
    space_change_listener_->OnBalloonSpaceChanged();
}

bool BalloonCollectionImpl::UpdateNotification(
    const Notification& notification) {
  Balloons::iterator iter = FindBalloon(notification);
  if (iter == balloons_.end())
    return false;
  Balloon* balloon = *iter;
  balloon->Update(notification);
  notification_ui_->Update(balloon);
  return true;
}

bool BalloonCollectionImpl::UpdateAndShowNotification(
    const Notification& notification) {
  Balloons::iterator iter = FindBalloon(notification);
  if (iter == balloons_.end())
    return false;
  Balloon* balloon = *iter;
  balloon->Update(notification);
  bool updated = notification_ui_->Update(balloon);
  DCHECK(updated);
  notification_ui_->Show(balloon);
  return true;
}

bool BalloonCollectionImpl::Remove(const Notification& notification) {
  Balloons::iterator iter = FindBalloon(notification);
  if (iter != balloons_.end()) {
    // Balloon.CloseByScript() will cause OnBalloonClosed() to be called on
    // this object, which will remove it from the collection and free it.
    (*iter)->CloseByScript();
    return true;
  }
  return false;
}

bool BalloonCollectionImpl::HasSpace() const {
  return true;
}

void BalloonCollectionImpl::ResizeBalloon(Balloon* balloon,
                                          const gfx::Size& size) {
  notification_ui_->ResizeNotification(balloon, size);
}

void BalloonCollectionImpl::OnBalloonClosed(Balloon* source) {
  // We want to free the balloon when finished.
  scoped_ptr<Balloon> closed(source);

  notification_ui_->Remove(source);

  Balloons::iterator iter = FindBalloon(source->notification());
  if (iter != balloons_.end()) {
    balloons_.erase(iter);
  }
  // There may be no listener in a unit test.
  if (space_change_listener_)
    space_change_listener_->OnBalloonSpaceChanged();
}

void BalloonCollectionImpl::Observe(NotificationType type,
                                    const NotificationSource& source,
                                    const NotificationDetails& details) {
  DCHECK(type == NotificationType::BROWSER_CLOSED);
  bool app_closing = *Details<bool>(details).ptr();
  // When exitting, we need to shutdown all renderers in
  // BalloonViewImpl before IO thread gets deleted in the
  // BrowserProcessImpl's destructor.  See http://crbug.com/40810
  // for details.
  if(app_closing)
    Shutdown();
}

void BalloonCollectionImpl::Shutdown() {
  // We need to remove the panel first because deleting
  // views that are not owned by parent will not remove
  // themselves from the parent.
  DLOG(INFO) << "Shutting down notification UI";
  notification_ui_.reset();
  STLDeleteElements(&balloons_);
}

Balloon* BalloonCollectionImpl::MakeBalloon(const Notification& notification,
                                            Profile* profile) {
  Balloon* new_balloon = new Balloon(notification, profile, this);
  new_balloon->set_view(new chromeos::BalloonViewImpl(false, true, false));
  return new_balloon;
}

std::deque<Balloon*>::iterator BalloonCollectionImpl::FindBalloon(
    const Notification& notification) {
  return std::find_if(balloons_.begin(),
                      balloons_.end(),
                      NotificationMatcher(notification));
}

}  // namespace chromeos

// static
BalloonCollection* BalloonCollection::Create() {
  return new chromeos::BalloonCollectionImpl();
}
