// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_LOGIN_USERNAME_VIEW_H_
#define CHROME_BROWSER_CHROMEOS_LOGIN_USERNAME_VIEW_H_
#pragma once

#include <string>

#include "views/controls/label.h"
#include "third_party/skia/include/core/SkBitmap.h"

// Forward declaration.
namespace gfx {
  class Rect;
}

namespace chromeos {
// Label with customized paddings and long text fade out.
class UsernameView : public views::Label {
 public:
  virtual ~UsernameView() {}

  // Overriden from views::Label.
  virtual void Paint(gfx::Canvas* canvas);

  // Returns the shaped username view to be used on the login screen. If
  // |user_small_shape| is true, then one pixel margins are used. This is done
  // to match the shape of the scaled frame of the user image. The caller gets
  // the ownership.
  static UsernameView* CreateShapedUsernameView(const std::wstring& username,
                                                bool use_small_shape);

 protected:
  // Constructs username view for the given |username|. Consider using
  // |CreateShapedUsernameView| to match the login page style.
  explicit UsernameView(const std::wstring& username);

 private:

  // Paints username to the bitmap with the bounds given.
  void PaintUsername(const gfx::Rect& bounds);

  // Holds painted username.
  scoped_ptr<SkBitmap> text_image_;

  // Holds margins width (depends on the height).
  int margin_width_;

  DISALLOW_COPY_AND_ASSIGN(UsernameView);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_LOGIN_USERNAME_VIEW_H_
