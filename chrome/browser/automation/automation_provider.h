// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This implements a browser-side endpoint for UI automation activity.
// The client-side endpoint is implemented by AutomationProxy.
// The entire lifetime of this object should be contained within that of
// the BrowserProcess, and in particular the NotificationService that's
// hung off of it.

#ifndef CHROME_BROWSER_AUTOMATION_AUTOMATION_PROVIDER_H_
#define CHROME_BROWSER_AUTOMATION_AUTOMATION_PROVIDER_H_
#pragma once

#include <map>
#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/observer_list.h"
#include "base/scoped_ptr.h"
#include "chrome/browser/autofill/field_types.h"
#include "chrome/browser/cancelable_request.h"
#include "chrome/browser/tab_contents/navigation_entry.h"
#include "chrome/common/content_settings.h"
#include "chrome/common/notification_observer.h"
#include "chrome/test/automation/automation_constants.h"
#include "ipc/ipc_message.h"
#include "ipc/ipc_channel.h"
#if defined(OS_WIN)
#include "gfx/native_widget_types.h"
#include "views/event.h"
#endif  // defined(OS_WIN)

struct AutomationMsg_Find_Params;
class PopupMenuWaiter;

namespace IPC {
struct Reposition_Params;
struct ExternalTabSettings;
class ChannelProxy;
}

class AutoFillProfile;
class AutomationAutocompleteEditTracker;
class AutomationBrowserTracker;
class AutomationExtensionTracker;
class AutomationResourceMessageFilter;
class AutomationTabTracker;
class AutomationWindowTracker;
class Browser;
class CreditCard;
class DictionaryValue;
class DownloadItem;
class Extension;
class ExtensionPortContainer;
class ExtensionTestResultNotificationObserver;
class ExternalTabContainer;
class FilePath;
class InitialLoadObserver;
class ListValue;
class LoginHandler;
class MetricEventDurationObserver;
class NavigationController;
class NavigationControllerRestoredObserver;
class Profile;
class RenderViewHost;
class TabContents;
struct AutocompleteMatchData;

namespace gfx {
class Point;
}

class AutomationProvider : public base::RefCounted<AutomationProvider>,
                           public IPC::Channel::Listener,
                           public IPC::Message::Sender {
 public:
  explicit AutomationProvider(Profile* profile);

  Profile* profile() const { return profile_; }

  // Establishes a connection to an automation client, if present.
  // An AutomationProxy should be established (probably in a different process)
  // before calling this.
  void ConnectToChannel(const std::string& channel_id);

  // Sets the number of tabs that we expect; when this number of tabs has
  // loaded, an AutomationMsg_InitialLoadsComplete message is sent.
  void SetExpectedTabCount(size_t expected_tabs);

  // Add a listener for navigation status notification. Currently only
  // navigation completion is observed; when the |number_of_navigations|
  // complete, the completed_response object is sent; if the server requires
  // authentication, we instead send the auth_needed_response object.  A pointer
  // to the added navigation observer is returned. This object should NOT be
  // deleted and should be released by calling the corresponding
  // RemoveNavigationStatusListener method.
  NotificationObserver* AddNavigationStatusListener(
      NavigationController* tab, IPC::Message* reply_message,
      int number_of_navigations, bool include_current_navigation);

  void RemoveNavigationStatusListener(NotificationObserver* obs);

  // Add an observer for the TabStrip. Currently only Tab append is observed. A
  // navigation listener is created on successful notification of tab append. A
  // pointer to the added navigation observer is returned. This object should
  // NOT be deleted and should be released by calling the corresponding
  // RemoveTabStripObserver method.
  NotificationObserver* AddTabStripObserver(Browser* parent,
                                            IPC::Message* reply_message);
  void RemoveTabStripObserver(NotificationObserver* obs);

  // Get the index of a particular NavigationController object
  // in the given parent window.  This method uses
  // TabStrip::GetIndexForNavigationController to get the index.
  int GetIndexForNavigationController(const NavigationController* controller,
                                      const Browser* parent) const;

  // Add or remove a non-owning reference to a tab's LoginHandler.  This is for
  // when a login prompt is shown for HTTP/FTP authentication.
  // TODO(mpcomplete): The login handling is a fairly special purpose feature.
  // Eventually we'll probably want ways to interact with the ChromeView of the
  // login window in a generic manner, such that it can be used for anything,
  // not just logins.
  void AddLoginHandler(NavigationController* tab, LoginHandler* handler);
  void RemoveLoginHandler(NavigationController* tab);

  // Add an extension port container.
  // Takes ownership of the container.
  void AddPortContainer(ExtensionPortContainer* port);
  // Remove and delete the port container.
  void RemovePortContainer(ExtensionPortContainer* port);
  // Get the port container for the given port id.
  ExtensionPortContainer* GetPortContainer(int port_id) const;

  // IPC implementations
  virtual bool Send(IPC::Message* msg);
  virtual void OnMessageReceived(const IPC::Message& msg);
  virtual void OnChannelError();

  IPC::Message* reply_message_release() {
    IPC::Message* reply_message = reply_message_;
    reply_message_ = NULL;
    return reply_message;
  }

  // Adds the extension passed in to the extension tracker, and returns
  // the associated handle. If the tracker already contains the extension,
  // the handle is simply returned.
  int AddExtension(Extension* extension);

#if defined(OS_WIN)
  // Adds the external tab passed in to the tab tracker.
  bool AddExternalTab(ExternalTabContainer* external_tab);
#endif

  // Get the DictionaryValue equivalent for a download item. Caller owns the
  // DictionaryValue.
  DictionaryValue* GetDictionaryFromDownloadItem(const DownloadItem* download);

 protected:
  friend class base::RefCounted<AutomationProvider>;
  virtual ~AutomationProvider();

  // Helper function to find the browser window that contains a given
  // NavigationController and activate that tab.
  // Returns the Browser if found.
  Browser* FindAndActivateTab(NavigationController* contents);

  // Convert a tab handle into a TabContents. If |tab| is non-NULL a pointer
  // to the tab is also returned. Returns NULL in case of failure or if the tab
  // is not of the TabContents type.
  TabContents* GetTabContentsForHandle(int handle, NavigationController** tab);

  scoped_ptr<AutomationAutocompleteEditTracker> autocomplete_edit_tracker_;
  scoped_ptr<AutomationBrowserTracker> browser_tracker_;
  scoped_ptr<InitialLoadObserver> initial_load_observer_;
  scoped_ptr<MetricEventDurationObserver> metric_event_duration_observer_;
  scoped_ptr<NavigationControllerRestoredObserver> restore_tracker_;
  scoped_ptr<AutomationTabTracker> tab_tracker_;
  scoped_ptr<AutomationWindowTracker> window_tracker_;

  typedef ObserverList<NotificationObserver> NotificationObserverList;
  NotificationObserverList notification_observer_list_;

  typedef std::map<NavigationController*, LoginHandler*> LoginHandlerMap;
  LoginHandlerMap login_handler_map_;

  Profile* profile_;

  // A pointer to reply message used when we do asynchronous processing in the
  // message handler.
  // TODO(phajdan.jr): Remove |reply_message_|, it is error-prone.
  IPC::Message* reply_message_;

  // Consumer for asynchronous history queries.
  CancelableRequestConsumer consumer_;

 private:
  void OnUnhandledMessage();

  // IPC Message callbacks.
  void WindowSimulateDrag(int handle,
                          std::vector<gfx::Point> drag_path,
                          int flags,
                          bool press_escape_en_route,
                          IPC::Message* reply_message);

#if defined(OS_WIN)
  // TODO(port): Replace HWND.
  void GetTabHWND(int handle, HWND* tab_hwnd);
#endif  // defined(OS_WIN)
  void HandleUnused(const IPC::Message& message, int handle);
  void SetFilteredInet(const IPC::Message& message, bool enabled);
  void GetFilteredInetHitCount(int* hit_count);
  void SetProxyConfig(const std::string& new_proxy_config);

  // Responds to the FindInPage request, retrieves the search query parameters,
  // launches an observer to listen for results and issues a StartFind request.
  void HandleFindRequest(int handle,
                         const AutomationMsg_Find_Params& params,
                         IPC::Message* reply_message);

  void OnSetPageFontSize(int tab_handle, int font_size);

  // See browsing_data_remover.h for explanation of bitmap fields.
  void RemoveBrowsingData(int remove_mask);

  void InstallExtension(const FilePath& crx_path,
                        IPC::Message* reply_message);

  void LoadExpandedExtension(const FilePath& extension_dir,
                             IPC::Message* reply_message);

  void GetEnabledExtensions(std::vector<FilePath>* result);

  void WaitForExtensionTestResult(IPC::Message* reply_message);

  void InstallExtensionAndGetHandle(const FilePath& crx_path,
                                    bool with_ui,
                                    IPC::Message* reply_message);

  void UninstallExtension(int extension_handle,
                          bool* success);

  void ReloadExtension(int extension_handle,
                       IPC::Message* reply_message);

  void EnableExtension(int extension_handle,
                       IPC::Message* reply_message);

  void DisableExtension(int extension_handle,
                        bool* success);

  void ExecuteExtensionActionInActiveTabAsync(int extension_handle,
                                              int browser_handle,
                                              IPC::Message* reply_message);

  void MoveExtensionBrowserAction(int extension_handle, int index,
                                  bool* success);

  void GetExtensionProperty(int extension_handle,
                            AutomationMsg_ExtensionProperty type,
                            bool* success,
                            std::string* value);

  // Asynchronous request for printing the current tab.
  void PrintAsync(int tab_handle);

  // Uses the specified encoding to override the encoding of the page in the
  // specified tab.
  void OverrideEncoding(int tab_handle,
                        const std::string& encoding_name,
                        bool* success);

  // Enables extension automation (for e.g. UITests).
  void SetEnableExtensionAutomation(
      int tab_handle,
      const std::vector<std::string>& functions_enabled);

  // Selects all contents on the page.
  void SelectAll(int tab_handle);

  // Edit operations on the page.
  void Cut(int tab_handle);
  void Copy(int tab_handle);
  void Paste(int tab_handle);

  void ReloadAsync(int tab_handle);
  void StopAsync(int tab_handle);
  void SaveAsAsync(int tab_handle);

#if defined(OS_CHROMEOS)
  // Logs in through the Chrome OS Login Wizard with given |username| and
  // password.  Returns true via |reply_message| on success.
  void LoginWithUserAndPass(const std::string& username,
                            const std::string& password,
                            IPC::Message* reply_message);
#endif

  // Returns the associated view for the tab handle passed in.
  // Returns NULL on failure.
  RenderViewHost* GetViewForTab(int tab_handle);

  // Returns the extension for the given handle. Returns NULL if there is
  // no extension for the handle.
  Extension* GetExtension(int extension_handle);

  // Returns the extension for the given handle, if the handle is valid and
  // the associated extension is enabled. Returns NULL otherwise.
  Extension* GetEnabledExtension(int extension_handle);

  // Returns the extension for the given handle, if the handle is valid and
  // the associated extension is disabled. Returns NULL otherwise.
  Extension* GetDisabledExtension(int extension_handle);

  // Method called by the popup menu tracker when a popup menu is opened.
  void NotifyPopupMenuOpened();

#if defined(OS_WIN)
  // The functions in this block are for use with external tabs, so they are
  // Windows only.

  // The container of an externally hosted tab calls this to reflect any
  // accelerator keys that it did not process. This gives the tab a chance
  // to handle the keys
  void ProcessUnhandledAccelerator(const IPC::Message& message, int handle,
                                   const MSG& msg);

  void SetInitialFocus(const IPC::Message& message, int handle, bool reverse,
                       bool restore_focus_to_view);

  void OnTabReposition(int tab_handle,
                       const IPC::Reposition_Params& params);

  void OnForwardContextMenuCommandToChrome(int tab_handle, int command);

  void CreateExternalTab(const IPC::ExternalTabSettings& settings,
                         gfx::NativeWindow* tab_container_window,
                         gfx::NativeWindow* tab_window,
                         int* tab_handle);

  void ConnectExternalTab(uint64 cookie,
                          bool allow,
                          gfx::NativeWindow parent_window,
                          gfx::NativeWindow* tab_container_window,
                          gfx::NativeWindow* tab_window,
                          int* tab_handle);

  void NavigateInExternalTab(
      int handle, const GURL& url, const GURL& referrer,
      AutomationMsg_NavigationResponseValues* status);
  void NavigateExternalTabAtIndex(
      int handle, int index, AutomationMsg_NavigationResponseValues* status);

  // Handler for a message sent by the automation client.
  void OnMessageFromExternalHost(int handle, const std::string& message,
                                 const std::string& origin,
                                 const std::string& target);

  // Determine if the message from the external host represents a browser
  // event, and if so dispatch it.
  bool InterceptBrowserEventMessageFromExternalHost(const std::string& message,
                                                    const std::string& origin,
                                                    const std::string& target);

  void OnBrowserMoved(int handle);

  void OnRunUnloadHandlers(int handle, IPC::Message* reply_message);

  void OnSetZoomLevel(int handle, int zoom_level);

  ExternalTabContainer* GetExternalTabForHandle(int handle);
#endif  // defined(OS_WIN)

  typedef std::map<int, ExtensionPortContainer*> PortContainerMap;

  scoped_ptr<IPC::ChannelProxy> channel_;
  scoped_ptr<NotificationObserver> new_tab_ui_load_observer_;
  scoped_ptr<NotificationObserver> find_in_page_observer_;
  scoped_ptr<NotificationObserver> dom_operation_observer_;
  scoped_ptr<NotificationObserver> dom_inspector_observer_;
  scoped_ptr<ExtensionTestResultNotificationObserver>
      extension_test_result_observer_;
  scoped_ptr<AutomationExtensionTracker> extension_tracker_;
  PortContainerMap port_containers_;
  scoped_refptr<AutomationResourceMessageFilter>
      automation_resource_message_filter_;

  DISALLOW_COPY_AND_ASSIGN(AutomationProvider);
};

#endif  // CHROME_BROWSER_AUTOMATION_AUTOMATION_PROVIDER_H_
