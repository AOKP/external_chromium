// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_USER_IMAGE_SCREEN_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_USER_IMAGE_SCREEN_H_

#include "chrome/browser/chromeos/login/camera.h"
#include "chrome/browser/chromeos/login/user_image_view.h"
#include "chrome/browser/chromeos/login/view_screen.h"
#include "chrome/common/notification_observer.h"
#include "chrome/common/notification_registrar.h"
#include "third_party/skia/include/core/SkBitmap.h"

namespace chromeos {

class UserImageScreen: public ViewScreen<UserImageView>,
                       public Camera::Delegate,
                       public UserImageView::Delegate,
                       public NotificationObserver {
 public:
  explicit UserImageScreen(WizardScreenDelegate* delegate);
  virtual ~UserImageScreen();

  // Overridden from ViewScreen:
  virtual void Refresh();
  virtual void Hide();
  virtual UserImageView* AllocateView();

  // Camera::Delegate implementation:
  virtual void OnVideoFrameCaptured(const SkBitmap& frame);

  // UserImageView::Delegate implementation:
  virtual void OnOK(const SkBitmap& image);
  virtual void OnCancel();

  // NotificationObserver implementation:
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

 private:
  // Object that handles video capturing.
  scoped_ptr<Camera> camera_;

  NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(UserImageScreen);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_USER_IMAGE_SCREEN_H_

