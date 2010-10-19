// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_BROWSING_DATA_LOCAL_STORAGE_HELPER_H_
#define CHROME_BROWSER_BROWSING_DATA_LOCAL_STORAGE_HELPER_H_
#pragma once

#include <string>
#include <vector>

#include "base/callback.h"
#include "base/file_path.h"
#include "base/scoped_ptr.h"
#include "base/time.h"
#include "chrome/common/url_constants.h"
#include "googleurl/src/gurl.h"

class Profile;

// This class fetches local storage information in the WebKit thread, and
// notifies the UI thread upon completion.
// A client of this class need to call StartFetching from the UI thread to
// initiate the flow, and it'll be notified by the callback in its UI
// thread at some later point.
// The client must call CancelNotification() if it's destroyed before the
// callback is notified.
class BrowsingDataLocalStorageHelper
    : public base::RefCountedThreadSafe<BrowsingDataLocalStorageHelper> {
 public:
  // Contains detailed information about local storage.
  struct LocalStorageInfo {
    LocalStorageInfo() {}
    LocalStorageInfo(
        const std::string& protocol,
        const std::string& host,
        unsigned short port,
        const std::string& database_identifier,
        const std::string& origin,
        const FilePath& file_path,
        int64 size,
        base::Time last_modified)
        : protocol(protocol),
          host(host),
          port(port),
          database_identifier(database_identifier),
          origin(origin),
          file_path(file_path),
          size(size),
          last_modified(last_modified) {
    }

    bool IsFileSchemeData() {
      return protocol == chrome::kFileScheme;
    }

    std::string protocol;
    std::string host;
    unsigned short port;
    std::string database_identifier;
    std::string origin;
    FilePath file_path;
    int64 size;
    base::Time last_modified;
  };

  explicit BrowsingDataLocalStorageHelper(Profile* profile);

  // Starts the fetching process, which will notify its completion via
  // callback.
  // This must be called only in the UI thread.
  virtual void StartFetching(
      Callback1<const std::vector<LocalStorageInfo>& >::Type* callback);
  // Cancels the notification callback (i.e., the window that created it no
  // longer exists).
  // This must be called only in the UI thread.
  virtual void CancelNotification();
  // Requests a single local storage file to be deleted in the WEBKIT thread.
  virtual void DeleteLocalStorageFile(const FilePath& file_path);

 protected:
  friend class base::RefCountedThreadSafe<BrowsingDataLocalStorageHelper>;
  virtual ~BrowsingDataLocalStorageHelper();

  Profile* profile_;

  // This only mutates in the WEBKIT thread.
  std::vector<LocalStorageInfo> local_storage_info_;

 private:
  // Enumerates all local storage files in the WEBKIT thread.
  void FetchLocalStorageInfoInWebKitThread();
  // Notifies the completion callback in the UI thread.
  void NotifyInUIThread();
  // Delete a single local storage file in the WEBKIT thread.
  void DeleteLocalStorageFileInWebKitThread(const FilePath& file_path);

  // This only mutates on the UI thread.
  scoped_ptr<Callback1<const std::vector<LocalStorageInfo>& >::Type >
      completion_callback_;
  // Indicates whether or not we're currently fetching information:
  // it's true when StartFetching() is called in the UI thread, and it's reset
  // after we notified the callback in the UI thread.
  // This only mutates on the UI thread.
  bool is_fetching_;

  DISALLOW_COPY_AND_ASSIGN(BrowsingDataLocalStorageHelper);
};

// This class is a thin wrapper around BrowsingDataLocalStorageHelper that does
// not fetch its information from the local storage tracker, but gets them
// passed as a parameter during construction.
class CannedBrowsingDataLocalStorageHelper
    : public BrowsingDataLocalStorageHelper {
 public:
  explicit CannedBrowsingDataLocalStorageHelper(Profile* profile);

  // Add a local storage to the set of canned local storages that is returned
  // by this helper.
  void AddLocalStorage(const GURL& origin);

  // Clear the list of canned local storages.
  void Reset();

  // True if no local storages are currently stored.
  bool empty() const;

  // BrowsingDataLocalStorageHelper methods.
  virtual void StartFetching(
      Callback1<const std::vector<LocalStorageInfo>& >::Type* callback);
  virtual void CancelNotification() {}

 private:
  virtual ~CannedBrowsingDataLocalStorageHelper() {}

  DISALLOW_COPY_AND_ASSIGN(CannedBrowsingDataLocalStorageHelper);
};

#endif  // CHROME_BROWSER_BROWSING_DATA_LOCAL_STORAGE_HELPER_H_
