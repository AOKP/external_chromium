// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/user_view.h"

#include "app/l10n_util.h"
#include "app/resource_bundle.h"
#include "chrome/browser/chromeos/login/helper.h"
#include "chrome/browser/chromeos/login/rounded_rect_painter.h"
#include "chrome/browser/chromeos/login/rounded_view.h"
#include "gfx/canvas.h"
#include "gfx/canvas_skia.h"
#include "gfx/gtk_util.h"
#include "gfx/rect.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "views/background.h"
#include "views/controls/button/text_button.h"
#include "views/controls/image_view.h"
#include "views/controls/label.h"
#include "views/controls/link.h"
#include "views/painter.h"

namespace {

// Background color and corner radius of the login status label and
// signout button.
const SkColor kSignoutBackgroundColor = 0xFF007700;
const int kSignoutBackgroundCornerRadius = 4;

// Horiz/Vert insets for Signout view.
const int kSignoutViewHorizontalInsets = 10;
const int kSignoutViewVerticalInsets = 5;

// Padding between remove button and top right image corner.
const int kRemoveButtonPadding = 3;

// Draws green-ish background for signout view with
// rounded corners at the bottom.
class SignoutBackgroundPainter : public views::Painter {
  virtual void Paint(int w, int h, gfx::Canvas* canvas) {
    SkRect rect = {0, 0, w, h};
    SkPath path;
    SkScalar corners[] = {
      0, 0,
      0, 0,
      kSignoutBackgroundCornerRadius,
      kSignoutBackgroundCornerRadius,
      kSignoutBackgroundCornerRadius,
      kSignoutBackgroundCornerRadius,
    };
    path.addRoundRect(rect, corners);
    SkPaint paint;
    paint.setStyle(SkPaint::kFill_Style);
    paint.setFlags(SkPaint::kAntiAlias_Flag);
    paint.setColor(kSignoutBackgroundColor);
    canvas->AsCanvasSkia()->drawPath(path, paint);
  }
};

}  // namespace

namespace chromeos {

using login::kBackgroundColor;
using login::kTextColor;
using login::kUserImageSize;

// The view that shows the Sign out button below the user's image.
class SignoutView : public views::View {
 public:
  explicit SignoutView(views::LinkController* link_controller) {
    ResourceBundle& rb = ResourceBundle::GetSharedInstance();
    const gfx::Font& font = rb.GetFont(ResourceBundle::SmallFont);

    active_user_label_ = new views::Label(
        l10n_util::GetString(IDS_SCREEN_LOCK_ACTIVE_USER));
    active_user_label_->SetFont(font);
    active_user_label_->SetColor(kTextColor);

    signout_link_ = new views::Link(
        l10n_util::GetString(IDS_SCREEN_LOCK_SIGN_OUT));
    signout_link_->SetController(link_controller);
    signout_link_->SetFont(font);
    signout_link_->SetColor(kTextColor);
    signout_link_->SetFocusable(true);

    AddChildView(active_user_label_);
    AddChildView(signout_link_);

    set_background(views::Background::CreateBackgroundPainter(
        true, new SignoutBackgroundPainter()));
  }

  // views::View overrides.
  virtual void Layout() {
    gfx::Size label = active_user_label_->GetPreferredSize();
    gfx::Size button = signout_link_->GetPreferredSize();
    active_user_label_->SetBounds(
        kSignoutViewHorizontalInsets, (height() - label.height()) / 2,
        label.width(), label.height());
    signout_link_->SetBounds(
        width() - button.width() - kSignoutViewHorizontalInsets,
        (height() - button.height()) / 2,
        button.width(), button.height());
  }

  virtual gfx::Size GetPreferredSize() {
    gfx::Size label = active_user_label_->GetPreferredSize();
    gfx::Size button = signout_link_->GetPreferredSize();
    return gfx::Size(label.width() + button.width(),
                     std::max(label.height(), button.height()) +
                     kSignoutViewVerticalInsets * 2);
  }

  views::Link* signout_link() { return signout_link_; }

 private:
  friend class UserView;

  views::Label* active_user_label_;
  views::Link* signout_link_;

  DISALLOW_COPY_AND_ASSIGN(SignoutView);
};

class RemoveButton : public views::TextButton {
 public:
  RemoveButton(views::ButtonListener* listener,
               const SkBitmap& icon,
               const std::wstring& text,
               const gfx::Point& top_right)
    : views::TextButton(listener, std::wstring()),
      icon_(icon),
      text_(text),
      top_right_(top_right),
      was_first_click_(false) {
    SetEnabledColor(SK_ColorWHITE);
    SetDisabledColor(SK_ColorWHITE);
    SetHighlightColor(SK_ColorWHITE);
    SetHoverColor(SK_ColorWHITE);
    SetIcon(icon_);
    UpdatePosition();
  }

 protected:
  // Overridden from View:
  virtual void OnMouseExited(const views::MouseEvent& event) {
    SetIcon(icon_);
    views::TextButton::SetText(std::wstring());
    ClearMaxTextSize();
    set_background(NULL);
    set_border(new views::TextButtonBorder);
    UpdatePosition();
    views::TextButton::OnMouseExited(event);
    was_first_click_ = false;
  }

  void NotifyClick(const views::Event& event) {
    if (!was_first_click_) {
      // On first click transform image to "remove" label.
      SetIcon(SkBitmap());
      views::TextButton::SetText(text_);

      const SkColor kStrokeColor = SK_ColorWHITE;
      const SkColor kButtonColor = 0xFFE94949;
      const int kStrokeWidth = 1;
      const int kVerticalPadding = 4;
      const int kHorizontalPadding = 8;
      const int kCornerRadius = 4;

      set_background(
          CreateRoundedBackground(
              kCornerRadius, kStrokeWidth, kButtonColor, kStrokeColor));

      set_border(
          views::Border::CreateEmptyBorder(kVerticalPadding,
                                           kHorizontalPadding,
                                           kVerticalPadding,
                                           kHorizontalPadding));

      UpdatePosition();
      was_first_click_ = true;
    } else {
      // On second click propagate to base class to fire ButtonPressed.
      views::TextButton::NotifyClick(event);
    }
  }

  void SetText(const std::wstring& text) {
    text_ = text;
  }

 private:
  // Update button position and schedule paint event for the view and parent.
  void UpdatePosition() {
    gfx::Size size = GetPreferredSize();
    gfx::Point origin = top_right_;
    origin.Offset(-size.width(), 0);
    SetBounds(gfx::Rect(origin, size));

    if (GetParent())
      GetParent()->SchedulePaint();
  }

  SkBitmap icon_;
  std::wstring text_;
  gfx::Point top_right_;
  bool was_first_click_;

  DISALLOW_COPY_AND_ASSIGN(RemoveButton);
};

class PodImageView : public views::ImageView {
 public:
  PodImageView() { }

  void SetImage(const SkBitmap& image, const SkBitmap& image_hot) {
    image_ = image;
    image_hot_ = image_hot;
    views::ImageView::SetImage(image_);
  }

 protected:
  // Overridden from View:
  virtual void OnMouseEntered(const views::MouseEvent& event) {
    views::ImageView::SetImage(image_hot_);
  }

  virtual void OnMouseExited(const views::MouseEvent& event) {
    views::ImageView::SetImage(image_);
  }

  gfx::NativeCursor GetCursorForPoint(
      views::Event::EventType event_type,
      const gfx::Point& p) {
    return gfx::GetCursor(GDK_HAND2);
  }

 private:
  SkBitmap image_;
  SkBitmap image_hot_;

  DISALLOW_COPY_AND_ASSIGN(PodImageView);
};

UserView::UserView(Delegate* delegate, bool is_login, bool need_background)
    : delegate_(delegate),
      signout_view_(NULL),
      image_view_(NULL),
      remove_button_(NULL) {
  DCHECK(delegate);
  if (!is_login)
    signout_view_ = new SignoutView(this);

  if (need_background)
    image_view_ = new RoundedView<PodImageView>;
  else
    image_view_ = new PodImageView;

  Init(need_background);
}

void UserView::Init(bool need_background) {
  if (need_background) {
    image_view_->set_background(
        views::Background::CreateSolidBackground(kBackgroundColor));
  }

  // UserView's layout never changes, so let's layout once here.
  image_view_->SetBounds(0, 0, kUserImageSize, kUserImageSize);
  AddChildView(image_view_);

  if (signout_view_) {
    signout_view_->SetBounds(0, kUserImageSize, kUserImageSize,
                             signout_view_->GetPreferredSize().height());
    AddChildView(signout_view_);
  }

  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  remove_button_ = new RemoveButton(
      this,
      *rb.GetBitmapNamed(IDR_CLOSE_BAR_H),
      l10n_util::GetString(IDS_LOGIN_REMOVE),
      gfx::Point(kUserImageSize - kRemoveButtonPadding, kRemoveButtonPadding));
  remove_button_->SetVisible(false);
  AddChildView(remove_button_);
}

void UserView::SetImage(const SkBitmap& image, const SkBitmap& image_hot) {
  int desired_size = std::min(image.width(), image.height());
  // Desired size is not preserved if it's greater than 75% of kUserImageSize.
  if (desired_size * 4 > 3 * kUserImageSize)
    desired_size = kUserImageSize;
  image_view_->SetImageSize(gfx::Size(desired_size, desired_size));
  image_view_->SetImage(image, image_hot);
}

void UserView::SetTooltipText(const std::wstring& text) {
  DCHECK(image_view_);
  image_view_->SetTooltipText(text);
}

gfx::Size UserView::GetPreferredSize() {
  return gfx::Size(
      kUserImageSize,
      kUserImageSize +
      (signout_view_ ? signout_view_->GetPreferredSize().height() : 0));
}

void UserView::SetSignoutEnabled(bool enabled) {
  DCHECK(signout_view_);
  signout_view_->signout_link_->SetEnabled(enabled);
}

void UserView::LinkActivated(views::Link* source, int event_flags) {
  DCHECK(delegate_);
  DCHECK(signout_view_);
  if (signout_view_->signout_link_ == source)
    delegate_->OnSignout();
}

void UserView::SetRemoveButtonVisible(bool flag) {
  remove_button_->SetVisible(flag);
}

void UserView::ButtonPressed(views::Button* sender, const views::Event& event) {
  DCHECK(delegate_);
  if (remove_button_ == sender)
    delegate_->OnRemoveUser();
}

void UserView::OnLocaleChanged() {
  remove_button_->SetText(l10n_util::GetString(IDS_LOGIN_REMOVE));
}

}  // namespace chromeos
