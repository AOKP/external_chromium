// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CONTENT_EXCEPTIONS_TABLE_MODEL_H_
#define CHROME_BROWSER_CONTENT_EXCEPTIONS_TABLE_MODEL_H_

#include <string>

#include "app/table_model.h"
#include "chrome/common/content_settings.h"
#include "chrome/common/content_settings_types.h"
#include "chrome/browser/host_content_settings_map.h"

class ContentExceptionsTableModel : public TableModel {
 public:
  ContentExceptionsTableModel(HostContentSettingsMap* map,
                              HostContentSettingsMap* off_the_record_map,
                              ContentSettingsType content_type);

  HostContentSettingsMap* map() const { return map_; }
  HostContentSettingsMap* off_the_record_map() const {
    return off_the_record_map_;
  }
  ContentSettingsType content_type() const { return content_type_; }

  bool entry_is_off_the_record(int index) {
    return index >= static_cast<int>(entries_.size());
  }

  const HostContentSettingsMap::PatternSettingPair& entry_at(int index) {
    return (entry_is_off_the_record(index) ?
            off_the_record_entries_[index - entries_.size()] : entries_[index]);
  }

  // Adds a new exception on the map and table model.
  void AddException(const HostContentSettingsMap::Pattern& pattern,
                    ContentSetting setting,
                    bool is_off_the_record);

  // Removes the exception at the specified index from both the map and model.
  void RemoveException(int row);

  // Removes all the exceptions from both the map and model.
  void RemoveAll();

  // Returns the index of the specified exception given a host, or -1 if there
  // is no exception for the specified host.
  int IndexOfExceptionByPattern(const HostContentSettingsMap::Pattern& pattern,
                                bool is_off_the_record);

  // TableModel overrides:
  virtual int RowCount();
  virtual std::wstring GetText(int row, int column_id);
  virtual void SetObserver(TableModelObserver* observer);

 private:
  HostContentSettingsMap* map(bool is_off_the_record) {
    return is_off_the_record ? off_the_record_map_ : map_;
  }
  HostContentSettingsMap::SettingsForOneType& entries(bool is_off_the_record) {
    return is_off_the_record ? off_the_record_entries_ : entries_;
  }

  HostContentSettingsMap* map_;
  HostContentSettingsMap* off_the_record_map_;
  ContentSettingsType content_type_;
  HostContentSettingsMap::SettingsForOneType entries_;
  HostContentSettingsMap::SettingsForOneType off_the_record_entries_;
  TableModelObserver* observer_;

  DISALLOW_COPY_AND_ASSIGN(ContentExceptionsTableModel);
};

#endif  // CHROME_BROWSER_CONTENT_EXCEPTIONS_TABLE_MODEL_H_
