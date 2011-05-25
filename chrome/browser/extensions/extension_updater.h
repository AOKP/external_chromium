// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_UPDATER_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_UPDATER_H_
#pragma once

#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/gtest_prod_util.h"
#include "base/ref_counted.h"
#include "base/scoped_ptr.h"
#include "base/scoped_temp_dir.h"
#include "base/task.h"
#include "base/time.h"
#include "base/timer.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/common/extensions/update_manifest.h"
#include "chrome/common/net/url_fetcher.h"
#include "googleurl/src/gurl.h"

class Extension;
class ExtensionUpdaterTest;
class ExtensionUpdaterFileHandler;
class PrefService;

// To save on server resources we can request updates for multiple extensions
// in one manifest check. This class helps us keep track of the id's for a
// given fetch, building up the actual URL, and what if anything to include
// in the ping parameter.
class ManifestFetchData {
 public:
  static const int kNeverPinged = -1;

  explicit ManifestFetchData(const GURL& update_url);
  ~ManifestFetchData();

  // Returns true if this extension information was successfully added. If the
  // return value is false it means the full_url would have become too long, and
  // this ManifestFetchData object remains unchanged.
  bool AddExtension(std::string id, std::string version, int ping_days,
                    const std::string& update_url_data);

  const GURL& base_url() const { return base_url_; }
  const GURL& full_url() const { return full_url_; }
  int extension_count() { return extension_ids_.size(); }
  const std::set<std::string>& extension_ids() const { return extension_ids_; }

  // Returns true if the given id is included in this manifest fetch.
  bool Includes(std::string extension_id) const;

  // Returns true if a ping parameter was added to full_url for this extension
  // id.
  bool DidPing(std::string extension_id) const;

 private:
  // Returns true if we should include a ping parameter for a given number of
  // days.
  bool ShouldPing(int days) const;

  std::set<std::string> extension_ids_;

  // Keeps track of the day value to use for the extensions where we want to
  // send a 'days since last ping' parameter in the check.
  std::map<std::string, int> ping_days_;

  // The base update url without any arguments added.
  GURL base_url_;

  // The base update url plus arguments indicating the id, version, etc.
  // information about each extension.
  GURL full_url_;

  DISALLOW_COPY_AND_ASSIGN(ManifestFetchData);
};

// A class for building a set of ManifestFetchData objects from
// extensions and pending extensions.
class ManifestFetchesBuilder {
 public:
  explicit ManifestFetchesBuilder(ExtensionUpdateService* service);
  ~ManifestFetchesBuilder();

  void AddExtension(const Extension& extension);

  void AddPendingExtension(const std::string& id,
                           const PendingExtensionInfo& info);

  // Adds all recorded stats taken so far to histogram counts.
  void ReportStats() const;

  // Caller takes ownership of the returned ManifestFetchData
  // objects.  Clears all recorded stats.
  std::vector<ManifestFetchData*> GetFetches();

 private:
  struct URLStats {
    URLStats()
        : no_url_count(0),
          google_url_count(0),
          other_url_count(0),
          extension_count(0),
          theme_count(0),
          app_count(0),
          pending_count(0) {}

    int no_url_count, google_url_count, other_url_count;
    int extension_count, theme_count, app_count, pending_count;
  };

  void AddExtensionData(Extension::Location location,
                        const std::string& id,
                        const Version& version,
                        Extension::Type extension_type,
                        GURL update_url,
                        const std::string& update_url_data);
  ExtensionUpdateService* service_;

  // List of data on fetches we're going to do. We limit the number of
  // extensions grouped together in one batch to avoid running into the limits
  // on the length of http GET requests, so there might be multiple
  // ManifestFetchData* objects with the same base_url.
  std::multimap<GURL, ManifestFetchData*> fetches_;

  URLStats url_stats_;

  DISALLOW_COPY_AND_ASSIGN(ManifestFetchesBuilder);
};

// A class for doing auto-updates of installed Extensions. Used like this:
//
// ExtensionUpdater* updater = new ExtensionUpdater(my_extensions_service,
//                                                  pref_service,
//                                                  update_frequency_secs);
// updater.Start();
// ....
// updater.Stop();
class ExtensionUpdater
    : public URLFetcher::Delegate,
      public base::RefCountedThreadSafe<ExtensionUpdater> {
 public:
  // Holds a pointer to the passed |service|, using it for querying installed
  // extensions and installing updated ones. The |frequency_seconds| parameter
  // controls how often update checks are scheduled.
  ExtensionUpdater(ExtensionUpdateService* service,
                   PrefService* prefs,
                   int frequency_seconds);

  // Starts the updater running.  Should be called at most once.
  void Start();

  // Stops the updater running, cancelling any outstanding update manifest and
  // crx downloads. Does not cancel any in-progress installs.
  void Stop();

  // Starts an update check right now, instead of waiting for the next regularly
  // scheduled check.
  void CheckNow();

  // Set blacklist checks on or off.
  void set_blacklist_checks_enabled(bool enabled) {
    blacklist_checks_enabled_ = enabled;
  }

 private:
  friend class base::RefCountedThreadSafe<ExtensionUpdater>;
  friend class ExtensionUpdaterTest;
  friend class ExtensionUpdaterFileHandler;
  friend class SafeManifestParser;

  virtual ~ExtensionUpdater();

  // We need to keep track of some information associated with a url
  // when doing a fetch.
  struct ExtensionFetch {
    std::string id;
    GURL url;
    std::string package_hash;
    std::string version;
    ExtensionFetch() : id(""), url(), package_hash(""), version("") {}
    ExtensionFetch(const std::string& i, const GURL& u,
      const std::string& h, const std::string& v)
      : id(i), url(u), package_hash(h), version(v) {}
  };

  // These are needed for unit testing, to help identify the correct mock
  // URLFetcher objects.
  static const int kManifestFetcherId = 1;
  static const int kExtensionFetcherId = 2;

  static const char* kBlacklistAppID;

  // Does common work from constructors.
  void Init();

  // Computes when to schedule the first update check.
  base::TimeDelta DetermineFirstCheckDelay();

  // URLFetcher::Delegate interface.
  virtual void OnURLFetchComplete(const URLFetcher* source,
                                  const GURL& url,
                                  const net::URLRequestStatus& status,
                                  int response_code,
                                  const ResponseCookies& cookies,
                                  const std::string& data);

  // These do the actual work when a URL fetch completes.
  virtual void OnManifestFetchComplete(const GURL& url,
                                       const net::URLRequestStatus& status,
                                       int response_code,
                                       const std::string& data);
  virtual void OnCRXFetchComplete(const GURL& url,
                                  const net::URLRequestStatus& status,
                                  int response_code,
                                  const std::string& data);

  // Called when a crx file has been written into a temp file, and is ready
  // to be installed.
  void OnCRXFileWritten(const std::string& id, const FilePath& path,
                        const GURL& download_url);

  // Verifies downloaded blacklist. Based on the blacklist, calls extension
  // service to unload blacklisted extensions and update pref.
  void ProcessBlacklist(const std::string& data);

  // Sets the timer to call TimerFired after roughly |target_delay| from now.
  // To help spread load evenly on servers, this method adds some random
  // jitter. It also saves the scheduled time so it can be reloaded on
  // browser restart.
  void ScheduleNextCheck(const base::TimeDelta& target_delay);

  // BaseTimer::ReceiverMethod callback.
  void TimerFired();

  // Begins an update check. Takes ownership of |fetch_data|.
  void StartUpdateCheck(ManifestFetchData* fetch_data);

  // Begins (or queues up) download of an updated extension.
  void FetchUpdatedExtension(const std::string& id, const GURL& url,
    const std::string& hash, const std::string& version);

  // Once a manifest is parsed, this starts fetches of any relevant crx files.
  void HandleManifestResults(const ManifestFetchData& fetch_data,
                             const UpdateManifest::Results& results);

  // Determines the version of an existing extension.
  // Returns true on success and false on failures.
  bool GetExistingVersion(const std::string& id, std::string* version);

  // Given a list of potential updates, returns the indices of the ones that are
  // applicable (are actually a new version, etc.) in |result|.
  std::vector<int> DetermineUpdates(const ManifestFetchData& fetch_data,
      const UpdateManifest::Results& possible_updates);

  // Whether Start() has been called but not Stop().
  bool alive_;

  // Outstanding url fetch requests for manifests and updates.
  scoped_ptr<URLFetcher> manifest_fetcher_;
  scoped_ptr<URLFetcher> extension_fetcher_;

  // Pending manifests and extensions to be fetched when the appropriate fetcher
  // is available.
  std::deque<ManifestFetchData*> manifests_pending_;
  std::deque<ExtensionFetch> extensions_pending_;

  // The manifest currently being fetched (if any).
  scoped_ptr<ManifestFetchData> current_manifest_fetch_;

  // The extension currently being fetched (if any).
  ExtensionFetch current_extension_fetch_;

  // Pointer back to the service that owns this ExtensionUpdater.
  ExtensionUpdateService* service_;

  base::OneShotTimer<ExtensionUpdater> timer_;
  int frequency_seconds_;

  PrefService* prefs_;

  scoped_refptr<ExtensionUpdaterFileHandler> file_handler_;
  bool blacklist_checks_enabled_;

  FRIEND_TEST(ExtensionUpdaterTest, TestStartUpdateCheckMemory);
  FRIEND_TEST(ExtensionUpdaterTest, TestAfterStopBehavior);

  DISALLOW_COPY_AND_ASSIGN(ExtensionUpdater);
};

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_UPDATER_H_
