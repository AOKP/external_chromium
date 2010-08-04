// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CUSTOM_HOME_PAGES_TABLE_MODEL_H_
#define CHROME_BROWSER_CUSTOM_HOME_PAGES_TABLE_MODEL_H_

#include <string>
#include <vector>

#include "app/table_model.h"
#include "chrome/browser/history/history.h"
#include "chrome/browser/favicon_service.h"
#include "googleurl/src/gurl.h"
#include "third_party/skia/include/core/SkBitmap.h"

class Profile;
class TableModelObserver;

// CustomHomePagesTableModel is the model for the TableView showing the list
// of pages the user wants opened on startup.

class CustomHomePagesTableModel : public TableModel {
 public:
  explicit CustomHomePagesTableModel(Profile* profile);
  virtual ~CustomHomePagesTableModel() {}

  // Sets the set of urls that this model contains.
  void SetURLs(const std::vector<GURL>& urls);

  // Adds an entry at the specified index.
  void Add(int index, const GURL& url);

  // Removes the entry at the specified index.
  void Remove(int index);

  // Clears any entries and fills the list with pages currently opened in the
  // browser.
  void SetToCurrentlyOpenPages();

  // Returns the set of urls this model contains.
  std::vector<GURL> GetURLs();

  // TableModel overrides:
  virtual int RowCount();
  virtual std::wstring GetText(int row, int column_id);
  virtual SkBitmap GetIcon(int row);
  virtual std::wstring GetTooltip(int row);
  virtual void SetObserver(TableModelObserver* observer);

 private:
  // Each item in the model is represented as an Entry. Entry stores the URL,
  // title, and favicon of the page.
  struct Entry {
    Entry() : title_handle(0), fav_icon_handle(0) {}

    // URL of the page.
    GURL url;

    // Page title.  If this is empty, we'll display the URL as the entry.
    std::wstring title;

    // Icon for the page.
    SkBitmap icon;

    // If non-zero, indicates we're loading the title for the page.
    HistoryService::Handle title_handle;

    // If non-zero, indicates we're loading the favicon for the page.
    FaviconService::Handle fav_icon_handle;
  };

  static void InitClass();

  // Loads the title and favicon for the specified entry.
  void LoadTitleAndFavIcon(Entry* entry);

  // Callback from history service. Updates the title of the Entry whose
  // |title_handle| matches |handle| and notifies the observer of the change.
  void OnGotTitle(HistoryService::Handle handle,
                  bool found_url,
                  const history::URLRow* row,
                  history::VisitVector* visits);

  // Callback from history service. Updates the icon of the Entry whose
  // |fav_icon_handle| matches |handle| and notifies the observer of the change.
  void OnGotFavIcon(FaviconService::Handle handle,
                    bool know_fav_icon,
                    scoped_refptr<RefCountedMemory> image_data,
                    bool is_expired,
                    GURL icon_url);

  // Returns the entry whose |member| matches |handle| and sets |entry_index| to
  // the index of the entry.
  Entry* GetEntryByLoadHandle(CancelableRequestProvider::Handle Entry::* member,
                              CancelableRequestProvider::Handle handle,
                              int* entry_index);

  // Returns the entry whose |fav_icon_handle| matches |handle| and sets
  // |entry_index| to the index of the entry.
  Entry* GetEntryByFavIconHandle(FaviconService::Handle handle,
                                 int* entry_index);

  // Returns the URL for a particular row, formatted for display to the user.
  std::wstring FormattedURL(int row) const;

  // Set of entries we're showing.
  std::vector<Entry> entries_;

  // Default icon to show when one can't be found for the URL.
  static SkBitmap default_favicon_;

  // Profile used to load titles and icons.
  Profile* profile_;

  TableModelObserver* observer_;

  // Used in loading titles and favicons.
  CancelableRequestConsumer query_consumer_;

  DISALLOW_COPY_AND_ASSIGN(CustomHomePagesTableModel);
};

#endif  // CHROME_BROWSER_CUSTOM_HOME_PAGES_TABLE_MODEL_H_
