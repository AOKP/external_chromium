// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/views/local_storage_set_item_info_view.h"

#include <algorithm>

#include "app/l10n_util.h"
#include "base/i18n/time_formatting.h"
#include "base/utf_string_conversions.h"
#include "gfx/color_utils.h"
#include "grit/generated_resources.h"
#include "views/grid_layout.h"
#include "views/controls/label.h"
#include "views/controls/textfield/textfield.h"
#include "views/standard_layout.h"

static const int kLocalStorageSetItemInfoViewBorderSize = 1;
static const int kLocalStorageSetItemInfoViewInsetSize = 3;

///////////////////////////////////////////////////////////////////////////////
// LocalStorageSetItemInfoView, public:

LocalStorageSetItemInfoView::LocalStorageSetItemInfoView()
    : host_value_field_(NULL),
      key_value_field_(NULL),
      value_value_field_(NULL) {
}

LocalStorageSetItemInfoView::~LocalStorageSetItemInfoView() {
}

void LocalStorageSetItemInfoView::SetFields(const std::string& host,
                                            const string16& key,
                                            const string16& value) {
  host_value_field_->SetText(UTF8ToWide(host));
  key_value_field_->SetText(key);
  value_value_field_->SetText(value);
  EnableLocalStorageDisplay(true);
}

void LocalStorageSetItemInfoView::EnableLocalStorageDisplay(bool enabled) {
  host_value_field_->SetEnabled(enabled);
  key_value_field_->SetEnabled(enabled);
  value_value_field_->SetEnabled(enabled);
}

void LocalStorageSetItemInfoView::ClearLocalStorageDisplay() {
  std::wstring no_cookie_string =
      l10n_util::GetString(IDS_COOKIES_COOKIE_NONESELECTED);
  host_value_field_->SetText(no_cookie_string);
  key_value_field_->SetText(no_cookie_string);
  value_value_field_->SetText(no_cookie_string);
  EnableLocalStorageDisplay(false);
}

///////////////////////////////////////////////////////////////////////////////
// LocalStorageSetItemInfoView, views::View overrides:

void LocalStorageSetItemInfoView::ViewHierarchyChanged(
    bool is_add, views::View* parent, views::View* child) {
  if (is_add && child == this)
    Init();
}

///////////////////////////////////////////////////////////////////////////////
// LocalStorageSetItemInfoView, private:

void LocalStorageSetItemInfoView::Init() {
  SkColor border_color = color_utils::GetSysSkColor(COLOR_3DSHADOW);
  views::Border* border = views::Border::CreateSolidBorder(
      kLocalStorageSetItemInfoViewBorderSize, border_color);
  set_border(border);

  // TODO(jorlow): These strings are not quite right, but we're post-freeze.
  views::Label* host_label = new views::Label(
      l10n_util::GetString(IDS_COOKIES_COOKIE_DOMAIN_LABEL));
  host_value_field_ = new views::Textfield;
  views::Label* key_label = new views::Label(
      l10n_util::GetString(IDS_COOKIES_LOCAL_STORAGE_KEY_LABEL));
  key_value_field_ = new views::Textfield;
  views::Label* value_label = new views::Label(
      l10n_util::GetString(IDS_COOKIES_LOCAL_STORAGE_VALUE_LABEL));
  value_value_field_ = new views::Textfield;

  using views::GridLayout;

  GridLayout* layout = new GridLayout(this);
  layout->SetInsets(kLocalStorageSetItemInfoViewInsetSize,
                    kLocalStorageSetItemInfoViewInsetSize,
                    kLocalStorageSetItemInfoViewInsetSize,
                    kLocalStorageSetItemInfoViewInsetSize);
  SetLayoutManager(layout);

  int three_column_layout_id = 0;
  views::ColumnSet* column_set = layout->AddColumnSet(three_column_layout_id);
  column_set->AddColumn(GridLayout::TRAILING, GridLayout::CENTER, 0,
                        GridLayout::USE_PREF, 0, 0);
  column_set->AddPaddingColumn(0, kRelatedControlHorizontalSpacing);
  column_set->AddColumn(GridLayout::FILL, GridLayout::FILL, 1,
                        GridLayout::USE_PREF, 0, 0);

  layout->StartRow(0, three_column_layout_id);
  layout->AddView(host_label);
  layout->AddView(host_value_field_);
  layout->AddPaddingRow(0, kRelatedControlSmallVerticalSpacing);
  layout->StartRow(0, three_column_layout_id);
  layout->AddView(key_label);
  layout->AddView(key_value_field_);
  layout->AddPaddingRow(0, kRelatedControlSmallVerticalSpacing);
  layout->StartRow(0, three_column_layout_id);
  layout->AddView(value_label);
  layout->AddView(value_value_field_);

  // Color these borderless text areas the same as the containing dialog.
  SkColor text_area_background = color_utils::GetSysSkColor(COLOR_3DFACE);
  // Now that the Textfields are in the view hierarchy, we can initialize them.
  host_value_field_->SetReadOnly(true);
  host_value_field_->RemoveBorder();
  host_value_field_->SetBackgroundColor(text_area_background);
  key_value_field_->SetReadOnly(true);
  key_value_field_->RemoveBorder();
  key_value_field_->SetBackgroundColor(text_area_background);
  value_value_field_->SetReadOnly(true);
  value_value_field_->RemoveBorder();
  value_value_field_->SetBackgroundColor(text_area_background);
}

