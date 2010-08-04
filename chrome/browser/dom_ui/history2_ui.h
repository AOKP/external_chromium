// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DOM_UI_HISTORY2_UI_H_
#define CHROME_BROWSER_DOM_UI_HISTORY2_UI_H_

#include <string>
#include <vector>

#include "base/string16.h"
#include "chrome/browser/dom_ui/chrome_url_data_manager.h"
#include "chrome/browser/dom_ui/dom_ui.h"
#include "chrome/browser/cancelable_request.h"
#include "chrome/browser/history/history.h"
#include "chrome/common/notification_registrar.h"

class GURL;

// Temporary fork for development of new history UI.
// TODO(pamg): merge back in when new UI is complete.

class HistoryUIHTMLSource2 : public ChromeURLDataManager::DataSource {
 public:
  HistoryUIHTMLSource2();

  // Called when the network layer has requested a resource underneath
  // the path we registered.
  virtual void StartDataRequest(const std::string& path,
                                bool is_off_the_record,
                                int request_id);

  virtual std::string GetMimeType(const std::string&) const {
    return "text/html";
  }

 private:
  ~HistoryUIHTMLSource2() {}

  DISALLOW_COPY_AND_ASSIGN(HistoryUIHTMLSource2);
};

// The handler for Javascript messages related to the "history" view.
class BrowsingHistoryHandler2 : public DOMMessageHandler,
                                public NotificationObserver {
 public:
  BrowsingHistoryHandler2();
  virtual ~BrowsingHistoryHandler2();

  // DOMMessageHandler implementation.
  virtual DOMMessageHandler* Attach(DOMUI* dom_ui);
  virtual void RegisterMessages();

  // Callback for the "getHistory" message.
  void HandleGetHistory(const Value* value);

  // Callback for the "searchHistory" message.
  void HandleSearchHistory(const Value* value);

  // Callback for the "removeURLsOnOneDay" message.
  void HandleRemoveURLsOnOneDay(const Value* value);

  // Handle for "clearBrowsingData" message.
  void HandleClearBrowsingData(const Value* value);

  // NotificationObserver implementation.
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

 private:
  // Callback from the history system when the history list is available.
  void QueryComplete(HistoryService::Handle request_handle,
                     history::QueryResults* results);

  // Callback from the history system when visits were deleted.
  void RemoveComplete();

  // Extract the arguments from the call to HandleSearchHistory.
  void ExtractSearchHistoryArguments(const Value* value,
                                     int* month,
                                     string16* query);

  // Figure out the query options for a month-wide query.
  history::QueryOptions CreateMonthQueryOptions(int month);

  NotificationRegistrar registrar_;

  // Current search text.
  string16 search_text_;

  // Our consumer for search requests to the history service.
  CancelableRequestConsumerT<int, 0> cancelable_search_consumer_;

  // Our consumer for delete requests to the history service.
  CancelableRequestConsumerT<int, 0> cancelable_delete_consumer_;

  DISALLOW_COPY_AND_ASSIGN(BrowsingHistoryHandler2);
};

class HistoryUI2 : public DOMUI {
 public:
  explicit HistoryUI2(TabContents* contents);

  // Return the URL for a given search term.
  static const GURL GetHistoryURLWithSearchText(const string16& text);

  static RefCountedMemory* GetFaviconResourceBytes();

 private:
  DISALLOW_COPY_AND_ASSIGN(HistoryUI2);
};

#endif  // CHROME_BROWSER_DOM_UI_HISTORY2_UI_H_
