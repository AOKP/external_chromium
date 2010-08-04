// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/autofill/autofill_cc_infobar.h"

#include "app/l10n_util.h"
#include "app/resource_bundle.h"
#include "chrome/browser/views/event_utils.h"
#include "chrome/browser/views/infobars/infobar_button_border.h"
#include "chrome/browser/views/infobars/infobars.h"
#include "chrome/common/pref_names.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "views/controls/button/text_button.h"
#include "views/controls/link.h"

class SaveCCInfoConfirmInfoBar : public AlertInfoBar,
                                 public views::LinkController  {
 public:
  explicit SaveCCInfoConfirmInfoBar(ConfirmInfoBarDelegate* delegate);
  virtual ~SaveCCInfoConfirmInfoBar();

  // Overridden from views::View:
  virtual void Layout();

  // Overridden from views::LinkController:
  virtual void LinkActivated(views::Link* source, int event_flags);

 protected:
  // Overridden from views::View:
  virtual void ViewHierarchyChanged(bool is_add,
                                    views::View* parent,
                                    views::View* child);

  // Overridden from views::ButtonListener:
  virtual void ButtonPressed(views::Button* sender, const views::Event& event);

  // Overridden from InfoBar:
  virtual int GetAvailableWidth() const;

 private:
  void Init();
  views::TextButton* CreateTextButton(const std::wstring& text);

  ConfirmInfoBarDelegate* GetDelegate();

  // The buttons are owned by InfoBar view from the moment they are added to its
  // hierarchy (Init() called), but we still need pointers to them to process
  // messages from them.
  views::TextButton* save_button_;
  views::TextButton* dont_save_button_;
  views::Link* link_;
  bool initialized_;

  DISALLOW_COPY_AND_ASSIGN(SaveCCInfoConfirmInfoBar);
};

SaveCCInfoConfirmInfoBar::SaveCCInfoConfirmInfoBar(
    ConfirmInfoBarDelegate* delegate)
    : AlertInfoBar(delegate),
      initialized_(false) {
  save_button_ = CreateTextButton(delegate->GetButtonLabel(
                                  ConfirmInfoBarDelegate::BUTTON_OK));
  dont_save_button_ = CreateTextButton(delegate->GetButtonLabel(
                                       ConfirmInfoBarDelegate::BUTTON_CANCEL));
  // Set up the link.
  link_ = new views::Link;
  link_->SetText(delegate->GetLinkText());
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  link_->SetFont(rb.GetFont(ResourceBundle::MediumFont));
  link_->SetHorizontalAlignment(views::Label::ALIGN_LEFT);
  link_->SetController(this);
  link_->MakeReadableOverBackgroundColor(background()->get_color());
}

SaveCCInfoConfirmInfoBar::~SaveCCInfoConfirmInfoBar() {
  if (!initialized_) {
    delete save_button_;
    delete dont_save_button_;
    delete link_;
  }
}

void SaveCCInfoConfirmInfoBar::Layout() {
  // Layout the close button.
  InfoBar::Layout();

  int available_width = AlertInfoBar::GetAvailableWidth();

  // Now append the link to the label's right edge.
  link_->SetVisible(!link_->GetText().empty());
  gfx::Size link_ps = link_->GetPreferredSize();
  int link_x = available_width - kButtonButtonSpacing - link_ps.width();
  link_->SetBounds(link_x, OffsetY(this, link_ps), link_ps.width(),
                   link_ps.height());
  available_width = link_x;

  // Layout the cancel and OK buttons.
  gfx::Size save_ps = save_button_->GetPreferredSize();
  gfx::Size dont_save_ps = dont_save_button_->GetPreferredSize();

  // Layout the icon and label.
  AlertInfoBar::Layout();

  int save_x = label()->bounds().right() + kEndOfLabelSpacing;
  save_x = std::max(0, std::min(save_x, available_width - (save_ps.width() +
                    dont_save_ps.width() + kButtonButtonSpacing)));

  save_button_->SetVisible(true);
  dont_save_button_->SetVisible(true);

  save_button_->SetBounds(save_x, OffsetY(this, save_ps), save_ps.width(),
                          save_ps.height());
  int dont_save_x = save_x + save_ps.width() + kButtonButtonSpacing;
  dont_save_button_->SetBounds(dont_save_x, OffsetY(this, dont_save_ps),
                               dont_save_ps.width(), dont_save_ps.height());
}

void SaveCCInfoConfirmInfoBar::LinkActivated(views::Link* source,
                                             int event_flags) {
  DCHECK(source == link_);
  DCHECK(link_->IsVisible());
  DCHECK(!link_->GetText().empty());
  GetDelegate()->LinkClicked(
      event_utils::DispositionFromEventFlags(event_flags));
}

void SaveCCInfoConfirmInfoBar::ViewHierarchyChanged(bool is_add,
                                                    views::View* parent,
                                                    views::View* child) {
  InfoBar::ViewHierarchyChanged(is_add, parent, child);
  if (is_add && child == this && !initialized_) {
    Init();
    initialized_ = true;
  }
}

void SaveCCInfoConfirmInfoBar::ButtonPressed(views::Button* sender,
                                             const views::Event& event) {
  InfoBar::ButtonPressed(sender, event);
  if (sender == save_button_) {
    if (GetDelegate()->Accept())
      RemoveInfoBar();
  } else if (sender == dont_save_button_) {
    if (GetDelegate()->Cancel())
      RemoveInfoBar();
  }
}

int SaveCCInfoConfirmInfoBar::GetAvailableWidth() const {
  int buttons_area_size = save_button_->GetPreferredSize().width() +
      dont_save_button_->GetPreferredSize().width() + kButtonButtonSpacing +
      kEndOfLabelSpacing;
  return std::max(0, link_->x() - buttons_area_size);
}

void SaveCCInfoConfirmInfoBar::Init() {
  AddChildView(save_button_);
  AddChildView(dont_save_button_);
  AddChildView(link_);
}

views::TextButton* SaveCCInfoConfirmInfoBar::CreateTextButton(
    const std::wstring& text) {
  views::TextButton* text_button = new views::TextButton(this, std::wstring());
  text_button->set_border(new InfoBarButtonBorder);

  // Set font colors for different states.
  text_button->SetEnabledColor(SK_ColorBLACK);
  text_button->SetHighlightColor(SK_ColorBLACK);
  text_button->SetHoverColor(SK_ColorBLACK);
  text_button->SetNormalHasBorder(true);
  text_button->SetShowHighlighted(true);

  // Set font then text, then size button to fit text.
  text_button->SetFont(ResourceBundle::GetSharedInstance().GetFont(
      ResourceBundle::MediumFont));
  text_button->SetText(text);
  text_button->ClearMaxTextSize();
  text_button->SizeToPreferredSize();
  return text_button;
}

ConfirmInfoBarDelegate* SaveCCInfoConfirmInfoBar::GetDelegate() {
  return delegate()->AsConfirmInfoBarDelegate();
}

InfoBar* CreateAutofillCcInfoBar(ConfirmInfoBarDelegate* delegate) {
  DCHECK(delegate);
  return new SaveCCInfoConfirmInfoBar(delegate);
}

