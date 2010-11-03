// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_AUTOMATION_AUTOMATION_PROVIDER_OBSERVERS_H_
#define CHROME_BROWSER_AUTOMATION_AUTOMATION_PROVIDER_OBSERVERS_H_
#pragma once

#include <deque>
#include <map>
#include <set>

#include "chrome/browser/bookmarks/bookmark_model_observer.h"
#include "chrome/browser/browsing_data_remover.h"
#include "chrome/browser/download/download_item.h"
#include "chrome/browser/download/download_manager.h"
#include "chrome/browser/history/history.h"
#include "chrome/browser/importer/importer.h"
#include "chrome/browser/importer/importer_data_types.h"
#include "chrome/browser/password_manager/password_store.h"
#include "chrome/browser/search_engines/template_url_model_observer.h"
#include "chrome/browser/tabs/tab_strip_model.h"
#include "chrome/common/notification_observer.h"
#include "chrome/common/notification_registrar.h"
#include "chrome/common/notification_type.h"
#include "chrome/test/automation/automation_messages.h"

class AutocompleteEditModel;
class AutomationProvider;
class Browser;
class Extension;
class ExtensionProcessManager;
class NavigationController;
class SavePackage;
class TabContents;
class TranslateInfoBarDelegate;

namespace IPC {
class Message;
}

class InitialLoadObserver : public NotificationObserver {
 public:
  InitialLoadObserver(size_t tab_count, AutomationProvider* automation);
  ~InitialLoadObserver();

  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

  // Caller owns the return value and is responsible for deleting it.
  // Example return value:
  // {'tabs': [{'start_time_ms': 1, 'stop_time_ms': 2.5},
  //           {'start_time_ms': 0.5, 'stop_time_ms': 3}]}
  // stop_time_ms values may be null if WaitForInitialLoads has not finished.
  // Only includes entries for the |tab_count| tabs we are monitoring.
  // There is no defined ordering of the return value.
  DictionaryValue* GetTimingInformation() const;

 private:
  class TabTime;
  typedef std::map<uintptr_t, TabTime> TabTimeMap;
  typedef std::set<uintptr_t> TabSet;

  void ConditionMet();

  NotificationRegistrar registrar_;

  AutomationProvider* automation_;
  size_t outstanding_tab_count_;
  base::TimeTicks init_time_;
  TabTimeMap loading_tabs_;
  TabSet finished_tabs_;

  DISALLOW_COPY_AND_ASSIGN(InitialLoadObserver);
};

// Watches for NewTabUI page loads for performance timing purposes.
class NewTabUILoadObserver : public NotificationObserver {
 public:
  explicit NewTabUILoadObserver(AutomationProvider* automation);
  ~NewTabUILoadObserver();

  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

 private:
  NotificationRegistrar registrar_;
  AutomationProvider* automation_;

  DISALLOW_COPY_AND_ASSIGN(NewTabUILoadObserver);
};

class NavigationControllerRestoredObserver : public NotificationObserver {
 public:
  NavigationControllerRestoredObserver(AutomationProvider* automation,
                                       NavigationController* controller,
                                       IPC::Message* reply_message);
  ~NavigationControllerRestoredObserver();

  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

 private:
  bool FinishedRestoring();
  void SendDone();

  NotificationRegistrar registrar_;
  AutomationProvider* automation_;
  NavigationController* controller_;
  IPC::Message* reply_message_;

  DISALLOW_COPY_AND_ASSIGN(NavigationControllerRestoredObserver);
};

class NavigationNotificationObserver : public NotificationObserver {
 public:
  NavigationNotificationObserver(NavigationController* controller,
                                 AutomationProvider* automation,
                                 IPC::Message* reply_message,
                                 int number_of_navigations,
                                 bool include_current_navigation);
  ~NavigationNotificationObserver();

  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

 private:
  void ConditionMet(AutomationMsg_NavigationResponseValues navigation_result);

  NotificationRegistrar registrar_;
  AutomationProvider* automation_;
  IPC::Message* reply_message_;
  NavigationController* controller_;
  int navigations_remaining_;
  bool navigation_started_;

  DISALLOW_COPY_AND_ASSIGN(NavigationNotificationObserver);
};

class TabStripNotificationObserver : public NotificationObserver {
 public:
  TabStripNotificationObserver(NotificationType notification,
                               AutomationProvider* automation);
  virtual ~TabStripNotificationObserver();

  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

  virtual void ObserveTab(NavigationController* controller) = 0;

 protected:
  NotificationRegistrar registrar_;
  AutomationProvider* automation_;
  NotificationType notification_;
};

class TabAppendedNotificationObserver : public TabStripNotificationObserver {
 public:
  TabAppendedNotificationObserver(Browser* parent,
                                  AutomationProvider* automation,
                                  IPC::Message* reply_message);

  virtual void ObserveTab(NavigationController* controller);

 protected:
  Browser* parent_;
  IPC::Message* reply_message_;

 private:
  DISALLOW_COPY_AND_ASSIGN(TabAppendedNotificationObserver);
};

class TabClosedNotificationObserver : public TabStripNotificationObserver {
 public:
  TabClosedNotificationObserver(AutomationProvider* automation,
                                bool wait_until_closed,
                                IPC::Message* reply_message);

  virtual void ObserveTab(NavigationController* controller);

  void set_for_browser_command(bool for_browser_command);

 protected:
  IPC::Message* reply_message_;
  bool for_browser_command_;

 private:
  DISALLOW_COPY_AND_ASSIGN(TabClosedNotificationObserver);
};

// Notifies when the tab count reaches the target number.
class TabCountChangeObserver : public TabStripModelObserver {
 public:
  TabCountChangeObserver(AutomationProvider* automation,
                         Browser* browser,
                         IPC::Message* reply_message,
                         int target_tab_count);
  // Implementation of TabStripModelObserver.
  virtual void TabInsertedAt(TabContents* contents,
                             int index,
                             bool foreground);
  virtual void TabDetachedAt(TabContents* contents, int index);
  virtual void TabStripModelDeleted();

 private:
  ~TabCountChangeObserver();

  // Checks if the current tab count matches our target, and if so,
  // sends the reply message and deletes self.
  void CheckTabCount();

  AutomationProvider* automation_;
  IPC::Message* reply_message_;

  TabStripModel* tab_strip_model_;

  const int target_tab_count_;

  DISALLOW_COPY_AND_ASSIGN(TabCountChangeObserver);
};

// Observes when an extension has finished installing or possible install
// errors. This does not guarantee that the extension is ready for use.
class ExtensionInstallNotificationObserver : public NotificationObserver {
 public:
  ExtensionInstallNotificationObserver(AutomationProvider* automation,
                                       int id,
                                       IPC::Message* reply_message);
  ~ExtensionInstallNotificationObserver();

  // Implementation of NotificationObserver.
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

 private:
  // Send |response| back to the provider's client.
  void SendResponse(AutomationMsg_ExtensionResponseValues response);

  NotificationRegistrar registrar_;
  scoped_refptr<AutomationProvider> automation_;
  int id_;
  IPC::Message* reply_message_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionInstallNotificationObserver);
};

// Observes when an extension has finished loading and is ready for use. Also
// checks for possible install errors.
class ExtensionReadyNotificationObserver : public NotificationObserver {
 public:
  ExtensionReadyNotificationObserver(ExtensionProcessManager* manager,
                                     AutomationProvider* automation,
                                     int id,
                                     IPC::Message* reply_message);
  ~ExtensionReadyNotificationObserver();

  // Implementation of NotificationObserver.
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

 private:
  NotificationRegistrar registrar_;
  ExtensionProcessManager* manager_;
  scoped_refptr<AutomationProvider> automation_;
  int id_;
  IPC::Message* reply_message_;
  Extension* extension_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionReadyNotificationObserver);
};

class ExtensionUnloadNotificationObserver : public NotificationObserver {
 public:
  ExtensionUnloadNotificationObserver();
  ~ExtensionUnloadNotificationObserver();

  // Implementation of NotificationObserver.
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

  bool did_receive_unload_notification() {
    return did_receive_unload_notification_;
  }

 private:
  NotificationRegistrar registrar_;
  bool did_receive_unload_notification_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionUnloadNotificationObserver);
};

class ExtensionTestResultNotificationObserver : public NotificationObserver {
 public:
  explicit ExtensionTestResultNotificationObserver(
      AutomationProvider* automation);
  ~ExtensionTestResultNotificationObserver();

  // Implementation of NotificationObserver.
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

  // Sends a test result back to the provider's client, if there is a pending
  // provider message and there is a result in the queue.
  void MaybeSendResult();

 private:
  NotificationRegistrar registrar_;
  AutomationProvider* automation_;
  // Two queues containing the test results. Although typically only
  // one result will be in each queue, there are cases where a queue is
  // needed.
  // For example, perhaps two events occur asynchronously and their
  // order of completion is not guaranteed. If the test wants to make sure
  // both finish before continuing, a queue is needed. The test would then
  // need to wait twice.
  std::deque<bool> results_;
  std::deque<std::string> messages_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionTestResultNotificationObserver);
};

class BrowserOpenedNotificationObserver : public NotificationObserver {
 public:
  BrowserOpenedNotificationObserver(AutomationProvider* automation,
                                    IPC::Message* reply_message);
  ~BrowserOpenedNotificationObserver();

  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

  void set_for_browser_command(bool for_browser_command);

 private:
  NotificationRegistrar registrar_;
  AutomationProvider* automation_;
  IPC::Message* reply_message_;
  bool for_browser_command_;

  DISALLOW_COPY_AND_ASSIGN(BrowserOpenedNotificationObserver);
};

class BrowserClosedNotificationObserver : public NotificationObserver {
 public:
  BrowserClosedNotificationObserver(Browser* browser,
                                    AutomationProvider* automation,
                                    IPC::Message* reply_message);

  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

  void set_for_browser_command(bool for_browser_command);

 private:
  NotificationRegistrar registrar_;
  AutomationProvider* automation_;
  IPC::Message* reply_message_;
  bool for_browser_command_;

  DISALLOW_COPY_AND_ASSIGN(BrowserClosedNotificationObserver);
};

class BrowserCountChangeNotificationObserver : public NotificationObserver {
 public:
  BrowserCountChangeNotificationObserver(int target_count,
                                         AutomationProvider* automation,
                                         IPC::Message* reply_message);

  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

 private:
  int target_count_;
  NotificationRegistrar registrar_;
  AutomationProvider* automation_;
  IPC::Message* reply_message_;

  DISALLOW_COPY_AND_ASSIGN(BrowserCountChangeNotificationObserver);
};

class AppModalDialogShownObserver : public NotificationObserver {
 public:
  AppModalDialogShownObserver(AutomationProvider* automation,
                              IPC::Message* reply_message);
  ~AppModalDialogShownObserver();

  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

 private:
  NotificationRegistrar registrar_;
  AutomationProvider* automation_;
  IPC::Message* reply_message_;

  DISALLOW_COPY_AND_ASSIGN(AppModalDialogShownObserver);
};

class ExecuteBrowserCommandObserver : public NotificationObserver {
 public:
  ~ExecuteBrowserCommandObserver();

  static bool CreateAndRegisterObserver(AutomationProvider* automation,
                                        Browser* browser,
                                        int command,
                                        IPC::Message* reply_message);

  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

 private:
  ExecuteBrowserCommandObserver(AutomationProvider* automation,
                                IPC::Message* reply_message);

  bool Register(int command);

  bool GetNotificationType(int command, NotificationType::Type* type);

  NotificationRegistrar registrar_;
  AutomationProvider* automation_;
  NotificationType::Type notification_type_;
  IPC::Message* reply_message_;

  DISALLOW_COPY_AND_ASSIGN(ExecuteBrowserCommandObserver);
};

class FindInPageNotificationObserver : public NotificationObserver {
 public:
  FindInPageNotificationObserver(AutomationProvider* automation,
                                 TabContents* parent_tab,
                                 bool reply_with_json,
                                 IPC::Message* reply_message);
  ~FindInPageNotificationObserver();

  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

  // The Find mechanism is over asynchronous IPC, so a search is kicked off and
  // we wait for notification to find out what the results are. As the user is
  // typing, new search requests can be issued and the Request ID helps us make
  // sense of whether this is the current request or an old one. The unit tests,
  // however, which uses this constant issues only one search at a time, so we
  // don't need a rolling id to identify each search. But, we still need to
  // specify one, so we just use a fixed one - its value does not matter.
  static const int kFindInPageRequestId;

 private:
  NotificationRegistrar registrar_;
  AutomationProvider* automation_;
  // We will at some point (before final update) be notified of the ordinal and
  // we need to preserve it so we can send it later.
  int active_match_ordinal_;
  // Send reply using json automation interface.
  bool reply_with_json_;
  IPC::Message* reply_message_;

  DISALLOW_COPY_AND_ASSIGN(FindInPageNotificationObserver);
};

class DomOperationNotificationObserver : public NotificationObserver {
 public:
  explicit DomOperationNotificationObserver(AutomationProvider* automation);
  ~DomOperationNotificationObserver();

  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

 private:
  NotificationRegistrar registrar_;
  AutomationProvider* automation_;

  DISALLOW_COPY_AND_ASSIGN(DomOperationNotificationObserver);
};

class DocumentPrintedNotificationObserver : public NotificationObserver {
 public:
  DocumentPrintedNotificationObserver(AutomationProvider* automation,
                                      IPC::Message* reply_message);
  ~DocumentPrintedNotificationObserver();

  virtual void Observe(NotificationType type, const NotificationSource& source,
                       const NotificationDetails& details);

 private:
  NotificationRegistrar registrar_;
  scoped_refptr<AutomationProvider> automation_;
  bool success_;
  IPC::Message* reply_message_;

  DISALLOW_COPY_AND_ASSIGN(DocumentPrintedNotificationObserver);
};

// Collects METRIC_EVENT_DURATION notifications and keep track of the times.
class MetricEventDurationObserver : public NotificationObserver {
 public:
  MetricEventDurationObserver();
  virtual ~MetricEventDurationObserver();

  // Get the duration of an event.  Returns -1 if we haven't seen the event.
  int GetEventDurationMs(const std::string& event_name);

  // NotificationObserver interface.
  virtual void Observe(NotificationType type, const NotificationSource& source,
                       const NotificationDetails& details);

 private:
  NotificationRegistrar registrar_;

  typedef std::map<std::string, int> EventDurationMap;
  EventDurationMap durations_;

  DISALLOW_COPY_AND_ASSIGN(MetricEventDurationObserver);
};

class PageTranslatedObserver : public NotificationObserver {
 public:
  PageTranslatedObserver(AutomationProvider* automation,
                         IPC::Message* reply_message,
                         TabContents* tab_contents);
  virtual ~PageTranslatedObserver();

  // NotificationObserver interface.
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

 private:
  NotificationRegistrar registrar_;
  scoped_refptr<AutomationProvider> automation_;
  IPC::Message* reply_message_;

  DISALLOW_COPY_AND_ASSIGN(PageTranslatedObserver);
};

class TabLanguageDeterminedObserver : public NotificationObserver {
 public:
  TabLanguageDeterminedObserver(AutomationProvider* automation,
                                IPC::Message* reply_message,
                                TabContents* tab_contents,
                                TranslateInfoBarDelegate* translate_bar);

  // NotificationObserver interface.
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

 private:
  NotificationRegistrar registrar_;
  AutomationProvider* automation_;
  IPC::Message* reply_message_;
  TabContents* tab_contents_;
  TranslateInfoBarDelegate* translate_bar_;

  DISALLOW_COPY_AND_ASSIGN(TabLanguageDeterminedObserver);
};

class InfoBarCountObserver : public NotificationObserver {
 public:
  InfoBarCountObserver(AutomationProvider* automation,
                       IPC::Message* reply_message,
                       TabContents* tab_contents,
                       int target_count);

  // NotificationObserver interface.
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

 private:
  // Checks whether the infobar count matches our target, and if so
  // sends the reply message and deletes itself.
  void CheckCount();

  NotificationRegistrar registrar_;
  AutomationProvider* automation_;
  IPC::Message* reply_message_;
  TabContents* tab_contents_;

  const int target_count_;

  DISALLOW_COPY_AND_ASSIGN(InfoBarCountObserver);
};

#if defined(OS_CHROMEOS)
// Collects LOGIN_AUTHENTICATION notifications and returns
// whether authentication succeeded to the automation provider.
class LoginManagerObserver : public NotificationObserver {
 public:
  LoginManagerObserver(AutomationProvider* automation,
                       IPC::Message* reply_message);

  // NotificationObserver interface.
  virtual void Observe(NotificationType type, const NotificationSource& source,
                       const NotificationDetails& details);

 private:
  NotificationRegistrar registrar_;
  AutomationProvider* automation_;
  IPC::Message* reply_message_;

  DISALLOW_COPY_AND_ASSIGN(LoginManagerObserver);
};
#endif

// Waits for the bookmark model to load.
class AutomationProviderBookmarkModelObserver : BookmarkModelObserver {
 public:
  AutomationProviderBookmarkModelObserver(AutomationProvider* provider,
                                          IPC::Message* reply_message,
                                          BookmarkModel* model);
  virtual ~AutomationProviderBookmarkModelObserver();

  virtual void Loaded(BookmarkModel* model) {
    ReplyAndDelete(true);
  }
  virtual void BookmarkModelBeingDeleted(BookmarkModel* model) {
    ReplyAndDelete(false);
  }
  virtual void BookmarkNodeMoved(BookmarkModel* model,
                                 const BookmarkNode* old_parent,
                                 int old_index,
                                 const BookmarkNode* new_parent,
                                 int new_index) {}
  virtual void BookmarkNodeAdded(BookmarkModel* model,
                                 const BookmarkNode* parent,
                                 int index) {}
  virtual void BookmarkNodeRemoved(BookmarkModel* model,
                                   const BookmarkNode* parent,
                                   int old_index,
                                   const BookmarkNode* node) {}
  virtual void BookmarkNodeChanged(BookmarkModel* model,
                                   const BookmarkNode* node) {}
  virtual void BookmarkNodeFavIconLoaded(BookmarkModel* model,
                                         const BookmarkNode* node) {}
  virtual void BookmarkNodeChildrenReordered(BookmarkModel* model,
                                             const BookmarkNode* node) {}

 private:
  // Reply to the automation message with the given success value,
  // then delete myself (which removes myself from the bookmark model
  // observer list).
  void ReplyAndDelete(bool success);

  scoped_refptr<AutomationProvider> automation_provider_;
  IPC::Message* reply_message_;
  BookmarkModel* model_;

  DISALLOW_COPY_AND_ASSIGN(AutomationProviderBookmarkModelObserver);
};

// Allows the automation provider to wait for all downloads to finish.
class AutomationProviderDownloadItemObserver : public DownloadItem::Observer {
 public:
  AutomationProviderDownloadItemObserver(
      AutomationProvider* provider,
      IPC::Message* reply_message,
      int downloads) {
    provider_ = provider;
    reply_message_ = reply_message;
    downloads_ = downloads;
  }
  virtual ~AutomationProviderDownloadItemObserver() {}

  virtual void OnDownloadUpdated(DownloadItem* download) { }
  virtual void OnDownloadFileCompleted(DownloadItem* download);
  virtual void OnDownloadOpened(DownloadItem* download) { }

 private:
  AutomationProvider* provider_;
  IPC::Message* reply_message_;
  int downloads_;

  DISALLOW_COPY_AND_ASSIGN(AutomationProviderDownloadItemObserver);
};

// Allows the automation provider to wait until the download has been updated
// or opened.
class AutomationProviderDownloadUpdatedObserver
    : public DownloadItem::Observer {
 public:
  AutomationProviderDownloadUpdatedObserver(
      AutomationProvider* provider,
      IPC::Message* reply_message,
      bool wait_for_open)
    : provider_(provider),
      reply_message_(reply_message),
      wait_for_open_(wait_for_open) {}

  virtual void OnDownloadUpdated(DownloadItem* download);
  virtual void OnDownloadOpened(DownloadItem* download);
  virtual void OnDownloadFileCompleted(DownloadItem* download) { }

 private:
  AutomationProvider* provider_;
  IPC::Message* reply_message_;
  bool wait_for_open_;

  DISALLOW_COPY_AND_ASSIGN(AutomationProviderDownloadUpdatedObserver);
};

// Allows the automation provider to wait until the download model has changed
// (because a new download has been added or removed).
class AutomationProviderDownloadModelChangedObserver
    : public DownloadManager::Observer {
 public:
  AutomationProviderDownloadModelChangedObserver(
      AutomationProvider* provider,
      IPC::Message* reply_message,
      DownloadManager* download_manager)
    : provider_(provider),
      reply_message_(reply_message),
      download_manager_(download_manager) {}

  virtual void ModelChanged();

 private:
  AutomationProvider* provider_;
  IPC::Message* reply_message_;
  DownloadManager* download_manager_;

  DISALLOW_COPY_AND_ASSIGN(AutomationProviderDownloadModelChangedObserver);
};

// Allows automation provider to wait until TemplateURLModel has loaded
// before looking up/returning search engine info.
class AutomationProviderSearchEngineObserver
    : public TemplateURLModelObserver {
 public:
  AutomationProviderSearchEngineObserver(
      AutomationProvider* provider,
      IPC::Message* reply_message)
    : provider_(provider),
      reply_message_(reply_message) {}

  void OnTemplateURLModelChanged();

 private:
  AutomationProvider* provider_;
  IPC::Message* reply_message_;

  DISALLOW_COPY_AND_ASSIGN(AutomationProviderSearchEngineObserver);
};

// Allows the automation provider to wait for history queries to finish.
class AutomationProviderHistoryObserver {
 public:
  AutomationProviderHistoryObserver(
      AutomationProvider* provider,
      IPC::Message* reply_message) {
    provider_ = provider;
    reply_message_ = reply_message;
  }
  ~AutomationProviderHistoryObserver() {}
  void HistoryQueryComplete(HistoryService::Handle request_handle,
                            history::QueryResults* results);

 private:
  AutomationProvider* provider_;
  IPC::Message* reply_message_;
};

// Allows the automation provider to wait for import queries to finish.
class AutomationProviderImportSettingsObserver
    : public ImporterHost::Observer {
 public:
  AutomationProviderImportSettingsObserver(
      AutomationProvider* provider,
      IPC::Message* reply_message)
    : provider_(provider),
      reply_message_(reply_message) {}
  void ImportStarted() {}
  void ImportItemStarted(importer::ImportItem item) {}
  void ImportItemEnded(importer::ImportItem item) {}
  void ImportEnded();
 private:
  AutomationProvider* provider_;
  IPC::Message* reply_message_;
};

// Allows automation provider to wait for getting passwords to finish.
class AutomationProviderGetPasswordsObserver
    : public PasswordStoreConsumer {
 public:
  AutomationProviderGetPasswordsObserver(
      AutomationProvider* provider,
      IPC::Message* reply_message)
    : provider_(provider),
      reply_message_(reply_message) {}

  void OnPasswordStoreRequestDone(
      int handle, const std::vector<webkit_glue::PasswordForm*>& result);

 private:
  AutomationProvider* provider_;
  IPC::Message* reply_message_;
};

// Allows the automation provider to wait for clearing browser data to finish.
class AutomationProviderBrowsingDataObserver
    : public BrowsingDataRemover::Observer {
 public:
  AutomationProviderBrowsingDataObserver(
      AutomationProvider* provider,
      IPC::Message* reply_message)
    : provider_(provider),
      reply_message_(reply_message) {}
  void OnBrowsingDataRemoverDone();

 private:
  AutomationProvider* provider_;
  IPC::Message* reply_message_;
};

// Allows automation provider to wait until page load after selecting an item
// in the omnibox popup.
class OmniboxAcceptNotificationObserver : public NotificationObserver {
 public:
  OmniboxAcceptNotificationObserver(NavigationController* controller,
                                 AutomationProvider* automation,
                                 IPC::Message* reply_message);
  ~OmniboxAcceptNotificationObserver();

  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

 private:
  NotificationRegistrar registrar_;
  AutomationProvider* automation_;
  IPC::Message* reply_message_;
  NavigationController* controller_;

  DISALLOW_COPY_AND_ASSIGN(OmniboxAcceptNotificationObserver);
};

// Allows the automation provider to wait for a save package notification.
class SavePackageNotificationObserver : public NotificationObserver {
 public:
  SavePackageNotificationObserver(SavePackage* save_package,
                                  AutomationProvider* automation,
                                  IPC::Message* reply_message);
  virtual ~SavePackageNotificationObserver() {}

  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

 private:
  NotificationRegistrar registrar_;
  AutomationProvider* automation_;
  IPC::Message* reply_message_;

  DISALLOW_COPY_AND_ASSIGN(SavePackageNotificationObserver);
};

// Allows automation provider to wait until the autocomplete edit
// has received focus
class AutocompleteEditFocusedObserver : public NotificationObserver {
 public:
  AutocompleteEditFocusedObserver(AutomationProvider* automation,
                                  AutocompleteEditModel* autocomplete_edit,
                                  IPC::Message* reply_message);

  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

 private:
  NotificationRegistrar registrar_;
  AutomationProvider* automation_;
  IPC::Message* reply_message_;
  AutocompleteEditModel* autocomplete_edit_model_;

  DISALLOW_COPY_AND_ASSIGN(AutocompleteEditFocusedObserver);
};

#endif  // CHROME_BROWSER_AUTOMATION_AUTOMATION_PROVIDER_OBSERVERS_H_
