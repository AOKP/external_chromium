// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_OPTIONS_EXCEPTIONS_PAGE_VIEW_H_
#define CHROME_BROWSER_UI_VIEWS_OPTIONS_EXCEPTIONS_PAGE_VIEW_H_
#pragma once

#include <vector>

#include "base/compiler_specific.h"
#include "chrome/browser/views/options/options_page_view.h"
#include "chrome/browser/views/options/passwords_page_view.h"
#include "views/controls/table/table_view_observer.h"

class Profile;

///////////////////////////////////////////////////////////////////////////////
// ExceptionsTableModel
class ExceptionsTableModel : public PasswordsTableModel {
 public:
  explicit ExceptionsTableModel(Profile* profile);
  virtual ~ExceptionsTableModel();

  // TableModel methods.
  virtual string16 GetText(int row, int column) OVERRIDE;
  virtual int CompareValues(int row1, int row2, int col_id) OVERRIDE;

  // PasswordStoreConsumer implementation.
  virtual void OnPasswordStoreRequestDone(
      int handle, const std::vector<webkit_glue::PasswordForm*>& result);
  // Request all logins data.
  void GetAllExceptionsForProfile();
};

///////////////////////////////////////////////////////////////////////////////
// ExceptionsPageView
class ExceptionsPageView : public OptionsPageView,
                           public views::TableViewObserver,
                           public views::ButtonListener,
                           public PasswordsTableModelObserver {
 public:
  explicit ExceptionsPageView(Profile* profile);
  virtual ~ExceptionsPageView();

  // views::TableViewObserverImplementation.
  virtual void OnSelectionChanged();

  // views::ButtonListener implementation.
  virtual void ButtonPressed(views::Button* sender, const views::Event& event);

  // PasswordsTableModelObserver implementation.
  virtual void OnRowCountChanged(size_t rows);

 protected:
  virtual void InitControlLayout();

 private:
  // Helper to configure our buttons and labels.
  void SetupButtons();

  // Helper to configure our table view.
  void SetupTable();

  ExceptionsTableModel table_model_;
  views::TableView* table_view_;

  // The buttons and labels.
  views::NativeButton remove_button_;
  views::NativeButton remove_all_button_;
  MultiLabelButtons show_button_;

  DISALLOW_COPY_AND_ASSIGN(ExceptionsPageView);
};

#endif  // CHROME_BROWSER_UI_VIEWS_OPTIONS_EXCEPTIONS_PAGE_VIEW_H_
