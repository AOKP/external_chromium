// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/automation/automation_provider_observers.h"

#include "base/basictypes.h"
#include "base/json/json_writer.h"
#include "base/string_util.h"
#include "chrome/app/chrome_dll_resource.h"
#include "chrome/browser/automation/automation_provider.h"
#include "chrome/browser/automation/automation_provider_json.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/browser.h"
#include "chrome/browser/browser_list.h"
#include "chrome/browser/dom_operation_notification_details.h"
#include "chrome/browser/download/download_item.h"
#include "chrome/browser/download/save_package.h"
#include "chrome/browser/extensions/extension_host.h"
#include "chrome/browser/extensions/extension_process_manager.h"
#include "chrome/browser/extensions/extension_updater.h"
#include "chrome/browser/login_prompt.h"
#include "chrome/browser/metrics/metric_event_duration_details.h"
#include "chrome/browser/printing/print_job.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/search_engines/template_url_model.h"
#include "chrome/browser/tab_contents/navigation_controller.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/browser/translate/page_translated_details.h"
#include "chrome/browser/translate/translate_infobar_delegate.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/notification_service.h"
#include "chrome/test/automation/automation_constants.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/login/authentication_notification_details.h"
#endif

// Holds onto start and stop timestamps for a particular tab
class InitialLoadObserver::TabTime {
 public:
  explicit TabTime(base::TimeTicks started)
      : load_start_time_(started) {
  }
  void set_stop_time(base::TimeTicks stopped) {
    load_stop_time_ = stopped;
  }
  base::TimeTicks stop_time() const {
    return load_stop_time_;
  }
  base::TimeTicks start_time() const {
    return load_start_time_;
  }
 private:
  base::TimeTicks load_start_time_;
  base::TimeTicks load_stop_time_;
};

InitialLoadObserver::InitialLoadObserver(size_t tab_count,
                                         AutomationProvider* automation)
    : automation_(automation),
      outstanding_tab_count_(tab_count),
      init_time_(base::TimeTicks::Now()) {
  if (outstanding_tab_count_ > 0) {
    registrar_.Add(this, NotificationType::LOAD_START,
                   NotificationService::AllSources());
    registrar_.Add(this, NotificationType::LOAD_STOP,
                   NotificationService::AllSources());
  }
}

InitialLoadObserver::~InitialLoadObserver() {
}

void InitialLoadObserver::Observe(NotificationType type,
                                  const NotificationSource& source,
                                  const NotificationDetails& details) {
  if (type == NotificationType::LOAD_START) {
    if (outstanding_tab_count_ > loading_tabs_.size())
      loading_tabs_.insert(TabTimeMap::value_type(
          source.map_key(),
          TabTime(base::TimeTicks::Now())));
  } else if (type == NotificationType::LOAD_STOP) {
    if (outstanding_tab_count_ > finished_tabs_.size()) {
      TabTimeMap::iterator iter = loading_tabs_.find(source.map_key());
      if (iter != loading_tabs_.end()) {
        finished_tabs_.insert(source.map_key());
        iter->second.set_stop_time(base::TimeTicks::Now());
      }
      if (outstanding_tab_count_ == finished_tabs_.size())
        ConditionMet();
    }
  } else {
    NOTREACHED();
  }
}

DictionaryValue* InitialLoadObserver::GetTimingInformation() const {
  ListValue* items = new ListValue;
  for (TabTimeMap::const_iterator it = loading_tabs_.begin();
       it != loading_tabs_.end();
       ++it) {
    DictionaryValue* item = new DictionaryValue;
    base::TimeDelta delta_start = it->second.start_time() - init_time_;

    item->SetReal("load_start_ms", delta_start.InMillisecondsF());
    if (it->second.stop_time().is_null()) {
      item->Set("load_stop_ms", Value::CreateNullValue());
    } else {
      base::TimeDelta delta_stop = it->second.stop_time() - init_time_;
      item->SetReal("load_stop_ms", delta_stop.InMillisecondsF());
    }
    items->Append(item);
  }
  DictionaryValue* return_value = new DictionaryValue;
  return_value->Set("tabs", items);
  return return_value;
}

void InitialLoadObserver::ConditionMet() {
  registrar_.RemoveAll();
  automation_->Send(new AutomationMsg_InitialLoadsComplete(0));
}

NewTabUILoadObserver::NewTabUILoadObserver(AutomationProvider* automation)
    : automation_(automation) {
  registrar_.Add(this, NotificationType::INITIAL_NEW_TAB_UI_LOAD,
                 NotificationService::AllSources());
}

NewTabUILoadObserver::~NewTabUILoadObserver() {
}

void NewTabUILoadObserver::Observe(NotificationType type,
                                   const NotificationSource& source,
                                   const NotificationDetails& details) {
  if (type == NotificationType::INITIAL_NEW_TAB_UI_LOAD) {
    Details<int> load_time(details);
    automation_->Send(
        new AutomationMsg_InitialNewTabUILoadComplete(0, *load_time.ptr()));
  } else {
    NOTREACHED();
  }
}

NavigationControllerRestoredObserver::NavigationControllerRestoredObserver(
    AutomationProvider* automation,
    NavigationController* controller,
    IPC::Message* reply_message)
    : automation_(automation),
      controller_(controller),
      reply_message_(reply_message) {
  if (FinishedRestoring()) {
    SendDone();
  } else {
    registrar_.Add(this, NotificationType::LOAD_STOP,
                   NotificationService::AllSources());
  }
}

NavigationControllerRestoredObserver::~NavigationControllerRestoredObserver() {
}

void NavigationControllerRestoredObserver::Observe(
    NotificationType type, const NotificationSource& source,
    const NotificationDetails& details) {
  if (FinishedRestoring()) {
    SendDone();
    registrar_.RemoveAll();
  }
}

bool NavigationControllerRestoredObserver::FinishedRestoring() {
  return (!controller_->needs_reload() && !controller_->pending_entry() &&
          !controller_->tab_contents()->is_loading());
}

void NavigationControllerRestoredObserver::SendDone() {
  DCHECK(reply_message_ != NULL);
  automation_->Send(reply_message_);
}

NavigationNotificationObserver::NavigationNotificationObserver(
    NavigationController* controller,
    AutomationProvider* automation,
    IPC::Message* reply_message,
    int number_of_navigations,
    bool include_current_navigation)
  : automation_(automation),
    reply_message_(reply_message),
    controller_(controller),
    navigations_remaining_(number_of_navigations),
    navigation_started_(false) {
  DCHECK_LT(0, navigations_remaining_);
  Source<NavigationController> source(controller_);
  registrar_.Add(this, NotificationType::NAV_ENTRY_COMMITTED, source);
  registrar_.Add(this, NotificationType::LOAD_START, source);
  registrar_.Add(this, NotificationType::LOAD_STOP, source);
  registrar_.Add(this, NotificationType::AUTH_NEEDED, source);
  registrar_.Add(this, NotificationType::AUTH_SUPPLIED, source);
  registrar_.Add(this, NotificationType::AUTH_CANCELLED, source);

  if (include_current_navigation && controller->tab_contents()->is_loading())
    navigation_started_ = true;
}

NavigationNotificationObserver::~NavigationNotificationObserver() {
  if (reply_message_) {
    // This means we did not receive a notification for this navigation.
    // Send over a failed navigation status back to the caller to ensure that
    // the caller does not hang waiting for the response.
    IPC::ParamTraits<AutomationMsg_NavigationResponseValues>::Write(
        reply_message_, AUTOMATION_MSG_NAVIGATION_ERROR);
    automation_->Send(reply_message_);
    reply_message_ = NULL;
  }

  automation_->RemoveNavigationStatusListener(this);
}

void NavigationNotificationObserver::Observe(
    NotificationType type, const NotificationSource& source,
    const NotificationDetails& details) {
  // We listen for 2 events to determine when the navigation started because:
  // - when this is used by the WaitForNavigation method, we might be invoked
  // afer the load has started (but not after the entry was committed, as
  // WaitForNavigation compares times of the last navigation).
  // - when this is used with a page requiring authentication, we will not get
  // a NotificationType::NAV_ENTRY_COMMITTED until after we authenticate, so
  // we need the NotificationType::LOAD_START.
  if (type == NotificationType::NAV_ENTRY_COMMITTED ||
      type == NotificationType::LOAD_START) {
    navigation_started_ = true;
  } else if (type == NotificationType::LOAD_STOP) {
    if (navigation_started_) {
      navigation_started_ = false;
      if (--navigations_remaining_ == 0)
        ConditionMet(AUTOMATION_MSG_NAVIGATION_SUCCESS);
    }
  } else if (type == NotificationType::AUTH_SUPPLIED ||
             type == NotificationType::AUTH_CANCELLED) {
    // The LoginHandler for this tab is no longer valid.
    automation_->RemoveLoginHandler(controller_);

    // Treat this as if navigation started again, since load start/stop don't
    // occur while authentication is ongoing.
    navigation_started_ = true;
  } else if (type == NotificationType::AUTH_NEEDED) {
    // Remember the login handler that wants authentication.
    // We do this in all cases (not just when navigation_started_ == true) so
    // tests can still wait for auth dialogs outside of navigation.
    LoginHandler* handler =
        Details<LoginNotificationDetails>(details)->handler();
    automation_->AddLoginHandler(controller_, handler);

    // Respond that authentication is needed.
    navigation_started_ = false;
    ConditionMet(AUTOMATION_MSG_NAVIGATION_AUTH_NEEDED);
  } else {
    NOTREACHED();
  }
}

void NavigationNotificationObserver::ConditionMet(
    AutomationMsg_NavigationResponseValues navigation_result) {
  DCHECK(reply_message_ != NULL);

  IPC::ParamTraits<AutomationMsg_NavigationResponseValues>::Write(
      reply_message_, navigation_result);
  automation_->Send(reply_message_);
  reply_message_ = NULL;

  delete this;
}

TabStripNotificationObserver::TabStripNotificationObserver(
    NotificationType notification, AutomationProvider* automation)
    : automation_(automation),
      notification_(notification) {
  registrar_.Add(this, notification_, NotificationService::AllSources());
}

TabStripNotificationObserver::~TabStripNotificationObserver() {
}

void TabStripNotificationObserver::Observe(NotificationType type,
                                           const NotificationSource& source,
                                           const NotificationDetails& details) {
  if (type == notification_) {
    ObserveTab(Source<NavigationController>(source).ptr());

    // If verified, no need to observe anymore
    automation_->RemoveTabStripObserver(this);
    delete this;
  } else {
    NOTREACHED();
  }
}

TabAppendedNotificationObserver::TabAppendedNotificationObserver(
    Browser* parent, AutomationProvider* automation,
    IPC::Message* reply_message)
    : TabStripNotificationObserver(NotificationType::TAB_PARENTED, automation),
      parent_(parent),
      reply_message_(reply_message) {
}

void TabAppendedNotificationObserver::ObserveTab(
    NavigationController* controller) {
  if (automation_->GetIndexForNavigationController(controller, parent_) ==
      TabStripModel::kNoTab) {
    // This tab notification doesn't belong to the parent_.
    return;
  }

  automation_->AddNavigationStatusListener(controller, reply_message_, 1,
                                           false);
}

TabClosedNotificationObserver::TabClosedNotificationObserver(
    AutomationProvider* automation, bool wait_until_closed,
    IPC::Message* reply_message)
    : TabStripNotificationObserver(wait_until_closed ?
          NotificationType::TAB_CLOSED : NotificationType::TAB_CLOSING,
          automation),
      reply_message_(reply_message),
      for_browser_command_(false) {
}

void TabClosedNotificationObserver::ObserveTab(
    NavigationController* controller) {
  if (for_browser_command_) {
    AutomationMsg_WindowExecuteCommand::WriteReplyParams(reply_message_,
                                                         true);
  } else {
    AutomationMsg_CloseTab::WriteReplyParams(reply_message_, true);
  }
  automation_->Send(reply_message_);
}

void TabClosedNotificationObserver::set_for_browser_command(
    bool for_browser_command) {
  for_browser_command_ = for_browser_command;
}

TabCountChangeObserver::TabCountChangeObserver(AutomationProvider* automation,
                                               Browser* browser,
                                               IPC::Message* reply_message,
                                               int target_tab_count)
    : automation_(automation),
      reply_message_(reply_message),
      tab_strip_model_(browser->tabstrip_model()),
      target_tab_count_(target_tab_count) {
  tab_strip_model_->AddObserver(this);
  CheckTabCount();
}

TabCountChangeObserver::~TabCountChangeObserver() {
  tab_strip_model_->RemoveObserver(this);
}

void TabCountChangeObserver::TabInsertedAt(TabContents* contents,
                                           int index,
                                           bool foreground) {
  CheckTabCount();
}

void TabCountChangeObserver::TabClosingAt(TabContents* contents, int index) {
  CheckTabCount();
}

void TabCountChangeObserver::TabStripModelDeleted() {
  AutomationMsg_WaitForTabCountToBecome::WriteReplyParams(reply_message_,
                                                          false);
  automation_->Send(reply_message_);
  delete this;
}

void TabCountChangeObserver::CheckTabCount() {
  if (tab_strip_model_->count() != target_tab_count_)
    return;

  AutomationMsg_WaitForTabCountToBecome::WriteReplyParams(reply_message_,
                                                          true);
  automation_->Send(reply_message_);
  delete this;
}

bool DidExtensionHostsStopLoading(ExtensionProcessManager* manager) {
  for (ExtensionProcessManager::const_iterator iter = manager->begin();
       iter != manager->end(); ++iter) {
    if (!(*iter)->did_stop_loading())
      return false;
  }
  return true;
}

ExtensionInstallNotificationObserver::ExtensionInstallNotificationObserver(
    AutomationProvider* automation, int id, IPC::Message* reply_message)
    : automation_(automation),
      id_(id),
      reply_message_(reply_message) {
  registrar_.Add(this, NotificationType::EXTENSION_LOADED,
                 NotificationService::AllSources());
  registrar_.Add(this, NotificationType::EXTENSION_INSTALL_ERROR,
                 NotificationService::AllSources());
  registrar_.Add(this, NotificationType::EXTENSION_UPDATE_DISABLED,
                 NotificationService::AllSources());
}

ExtensionInstallNotificationObserver::~ExtensionInstallNotificationObserver() {
}

void ExtensionInstallNotificationObserver::Observe(
    NotificationType type, const NotificationSource& source,
    const NotificationDetails& details) {
  switch (type.value) {
    case NotificationType::EXTENSION_LOADED:
      SendResponse(AUTOMATION_MSG_EXTENSION_INSTALL_SUCCEEDED);
      break;
    case NotificationType::EXTENSION_INSTALL_ERROR:
    case NotificationType::EXTENSION_UPDATE_DISABLED:
      SendResponse(AUTOMATION_MSG_EXTENSION_INSTALL_FAILED);
      break;
    default:
      NOTREACHED();
      break;
  }

  delete this;
}

void ExtensionInstallNotificationObserver::SendResponse(
    AutomationMsg_ExtensionResponseValues response) {
  if (reply_message_ != NULL) {
    switch (id_) {
      case AutomationMsg_InstallExtension::ID:
        AutomationMsg_InstallExtension::WriteReplyParams(reply_message_,
                                                         response);
        break;
      case AutomationMsg_LoadExpandedExtension::ID:
        AutomationMsg_LoadExpandedExtension::WriteReplyParams(reply_message_,
                                                              response);
        break;
      default:
        NOTREACHED();
        break;
    }

    automation_->Send(reply_message_);
    reply_message_ = NULL;
  }
}

ExtensionReadyNotificationObserver::ExtensionReadyNotificationObserver(
    ExtensionProcessManager* manager, AutomationProvider* automation, int id,
    IPC::Message* reply_message)
    : manager_(manager),
      automation_(automation),
      id_(id),
      reply_message_(reply_message),
      extension_(NULL) {
  registrar_.Add(this, NotificationType::EXTENSION_HOST_DID_STOP_LOADING,
                 NotificationService::AllSources());
  registrar_.Add(this, NotificationType::EXTENSION_LOADED,
                 NotificationService::AllSources());
  registrar_.Add(this, NotificationType::EXTENSION_INSTALL_ERROR,
                 NotificationService::AllSources());
  registrar_.Add(this, NotificationType::EXTENSION_UPDATE_DISABLED,
                 NotificationService::AllSources());
}

ExtensionReadyNotificationObserver::~ExtensionReadyNotificationObserver() {
}

void ExtensionReadyNotificationObserver::Observe(
    NotificationType type, const NotificationSource& source,
    const NotificationDetails& details) {
  bool success = false;
  switch (type.value) {
    case NotificationType::EXTENSION_HOST_DID_STOP_LOADING:
      // Only continue on with this method if our extension has been loaded
      // and all the extension hosts have stopped loading.
      if (!extension_ || !DidExtensionHostsStopLoading(manager_))
        return;
      success = true;
      break;
    case NotificationType::EXTENSION_LOADED:
      extension_ = Details<Extension>(details).ptr();
      if (!DidExtensionHostsStopLoading(manager_))
        return;
      success = true;
      break;
    case NotificationType::EXTENSION_INSTALL_ERROR:
    case NotificationType::EXTENSION_UPDATE_DISABLED:
      success = false;
      break;
    default:
      NOTREACHED();
      break;
  }

  if (id_ == AutomationMsg_InstallExtensionAndGetHandle::ID) {
    // A handle of zero indicates an error.
    int extension_handle = 0;
    if (extension_)
      extension_handle = automation_->AddExtension(extension_);
    AutomationMsg_InstallExtensionAndGetHandle::WriteReplyParams(
        reply_message_, extension_handle);
  } else if (id_ == AutomationMsg_EnableExtension::ID) {
    AutomationMsg_EnableExtension::WriteReplyParams(reply_message_, true);
  } else {
    NOTREACHED();
    LOG(ERROR) << "Cannot write reply params for unknown message id.";
  }

  automation_->Send(reply_message_);
  delete this;
}

ExtensionUnloadNotificationObserver::ExtensionUnloadNotificationObserver()
    : did_receive_unload_notification_(false) {
  registrar_.Add(this, NotificationType::EXTENSION_UNLOADED,
                 NotificationService::AllSources());
  registrar_.Add(this, NotificationType::EXTENSION_UNLOADED_DISABLED,
                 NotificationService::AllSources());
}

ExtensionUnloadNotificationObserver::~ExtensionUnloadNotificationObserver() {
}

void ExtensionUnloadNotificationObserver::Observe(
    NotificationType type, const NotificationSource& source,
    const NotificationDetails& details) {
  if (type.value == NotificationType::EXTENSION_UNLOADED ||
      type.value == NotificationType::EXTENSION_UNLOADED_DISABLED) {
    did_receive_unload_notification_ = true;
  } else {
    NOTREACHED();
  }
}

ExtensionTestResultNotificationObserver::
    ExtensionTestResultNotificationObserver(AutomationProvider* automation)
    : automation_(automation) {
  registrar_.Add(this, NotificationType::EXTENSION_TEST_PASSED,
                 NotificationService::AllSources());
  registrar_.Add(this, NotificationType::EXTENSION_TEST_FAILED,
                 NotificationService::AllSources());
}

ExtensionTestResultNotificationObserver::
    ~ExtensionTestResultNotificationObserver() {
}

void ExtensionTestResultNotificationObserver::Observe(
    NotificationType type, const NotificationSource& source,
    const NotificationDetails& details) {
  switch (type.value) {
    case NotificationType::EXTENSION_TEST_PASSED:
      results_.push_back(true);
      messages_.push_back("");
      break;

    case NotificationType::EXTENSION_TEST_FAILED:
      results_.push_back(false);
      messages_.push_back(*(Details<std::string>(details).ptr()));
      break;

    default:
      NOTREACHED();
  }
  // There may be a reply message waiting for this event, so check.
  MaybeSendResult();
}

void ExtensionTestResultNotificationObserver::MaybeSendResult() {
  if (results_.size() > 0) {
    // This release method should return the automation's current
    // reply message, or NULL if there is no current one. If it is not
    // NULL, we are stating that we will handle this reply message.
    IPC::Message* reply_message = automation_->reply_message_release();
    // Send the result back if we have a reply message.
    if (reply_message) {
      AutomationMsg_WaitForExtensionTestResult::WriteReplyParams(
          reply_message, results_.front(), messages_.front());
      results_.pop_front();
      messages_.pop_front();
      automation_->Send(reply_message);
    }
  }
}

BrowserOpenedNotificationObserver::BrowserOpenedNotificationObserver(
    AutomationProvider* automation, IPC::Message* reply_message)
    : automation_(automation),
      reply_message_(reply_message),
      for_browser_command_(false) {
  registrar_.Add(this, NotificationType::BROWSER_OPENED,
                 NotificationService::AllSources());
}

BrowserOpenedNotificationObserver::~BrowserOpenedNotificationObserver() {
}

void BrowserOpenedNotificationObserver::Observe(
    NotificationType type, const NotificationSource& source,
    const NotificationDetails& details) {
  if (type == NotificationType::BROWSER_OPENED) {
    if (for_browser_command_) {
      AutomationMsg_WindowExecuteCommand::WriteReplyParams(reply_message_,
                                                           true);
    }
    automation_->Send(reply_message_);
    delete this;
  } else {
    NOTREACHED();
  }
}

void BrowserOpenedNotificationObserver::set_for_browser_command(
    bool for_browser_command) {
  for_browser_command_ = for_browser_command;
}

BrowserClosedNotificationObserver::BrowserClosedNotificationObserver(
    Browser* browser,
    AutomationProvider* automation,
    IPC::Message* reply_message)
    : automation_(automation),
      reply_message_(reply_message),
      for_browser_command_(false) {
  registrar_.Add(this, NotificationType::BROWSER_CLOSED,
                 Source<Browser>(browser));
}

void BrowserClosedNotificationObserver::Observe(
    NotificationType type, const NotificationSource& source,
    const NotificationDetails& details) {
  DCHECK(type == NotificationType::BROWSER_CLOSED);
  Details<bool> close_app(details);
  DCHECK(reply_message_ != NULL);
  if (for_browser_command_) {
    AutomationMsg_WindowExecuteCommand::WriteReplyParams(reply_message_,
                                                         true);
  } else {
    AutomationMsg_CloseBrowser::WriteReplyParams(reply_message_, true,
                                                 *(close_app.ptr()));
  }
  automation_->Send(reply_message_);
  reply_message_ = NULL;
  delete this;
}

void BrowserClosedNotificationObserver::set_for_browser_command(
    bool for_browser_command) {
  for_browser_command_ = for_browser_command;
}

BrowserCountChangeNotificationObserver::BrowserCountChangeNotificationObserver(
    int target_count,
    AutomationProvider* automation,
    IPC::Message* reply_message)
    : target_count_(target_count),
      automation_(automation),
      reply_message_(reply_message) {
  registrar_.Add(this, NotificationType::BROWSER_OPENED,
                 NotificationService::AllSources());
  registrar_.Add(this, NotificationType::BROWSER_CLOSED,
                 NotificationService::AllSources());
}

void BrowserCountChangeNotificationObserver::Observe(
    NotificationType type, const NotificationSource& source,
    const NotificationDetails& details) {
  DCHECK(type == NotificationType::BROWSER_OPENED ||
         type == NotificationType::BROWSER_CLOSED);
  int current_count = static_cast<int>(BrowserList::size());
  if (type == NotificationType::BROWSER_CLOSED) {
    // At the time of the notification the browser being closed is not removed
    // from the list. The real count is one less than the reported count.
    DCHECK_LT(0, current_count);
    current_count--;
  }
  if (current_count == target_count_) {
    AutomationMsg_WaitForBrowserWindowCountToBecome::WriteReplyParams(
        reply_message_, true);
    automation_->Send(reply_message_);
    reply_message_ = NULL;
    delete this;
  }
}

AppModalDialogShownObserver::AppModalDialogShownObserver(
    AutomationProvider* automation, IPC::Message* reply_message)
    : automation_(automation),
      reply_message_(reply_message) {
  registrar_.Add(this, NotificationType::APP_MODAL_DIALOG_SHOWN,
                 NotificationService::AllSources());
}

AppModalDialogShownObserver::~AppModalDialogShownObserver() {
}

void AppModalDialogShownObserver::Observe(
    NotificationType type, const NotificationSource& source,
    const NotificationDetails& details) {
  DCHECK(type == NotificationType::APP_MODAL_DIALOG_SHOWN);

  AutomationMsg_WaitForAppModalDialogToBeShown::WriteReplyParams(
      reply_message_, true);
  automation_->Send(reply_message_);
  reply_message_ = NULL;
  delete this;
}

namespace {

// Define mapping from command to notification
struct CommandNotification {
  int command;
  NotificationType::Type notification_type;
};

const struct CommandNotification command_notifications[] = {
  {IDC_DUPLICATE_TAB, NotificationType::TAB_PARENTED},
  {IDC_NEW_TAB, NotificationType::INITIAL_NEW_TAB_UI_LOAD},

  // Returns as soon as the restored tab is created. To further wait until
  // the content page is loaded, use WaitForTabToBeRestored.
  {IDC_RESTORE_TAB, NotificationType::TAB_PARENTED},

  // For the following commands, we need to wait for a new tab to be created,
  // load to finish, and title to change.
  {IDC_MANAGE_EXTENSIONS, NotificationType::TAB_CONTENTS_TITLE_UPDATED},
  {IDC_OPTIONS, NotificationType::TAB_CONTENTS_TITLE_UPDATED},
  {IDC_SHOW_DOWNLOADS, NotificationType::TAB_CONTENTS_TITLE_UPDATED},
  {IDC_SHOW_HISTORY, NotificationType::TAB_CONTENTS_TITLE_UPDATED},
};

}  // namespace

ExecuteBrowserCommandObserver::~ExecuteBrowserCommandObserver() {
}

// static
bool ExecuteBrowserCommandObserver::CreateAndRegisterObserver(
    AutomationProvider* automation, Browser* browser, int command,
    IPC::Message* reply_message) {
  bool result = true;
  switch (command) {
    case IDC_NEW_WINDOW:
    case IDC_NEW_INCOGNITO_WINDOW: {
      BrowserOpenedNotificationObserver* observer =
          new BrowserOpenedNotificationObserver(automation, reply_message);
      observer->set_for_browser_command(true);
      break;
    }
    case IDC_CLOSE_WINDOW: {
      BrowserClosedNotificationObserver* observer =
          new BrowserClosedNotificationObserver(browser, automation,
                                                reply_message);
      observer->set_for_browser_command(true);
      break;
    }
    case IDC_CLOSE_TAB: {
      TabClosedNotificationObserver* observer =
          new TabClosedNotificationObserver(automation, true, reply_message);
      observer->set_for_browser_command(true);
      break;
    }
    case IDC_BACK:
    case IDC_FORWARD:
    case IDC_RELOAD: {
      automation->AddNavigationStatusListener(
          &browser->GetSelectedTabContents()->controller(),
          reply_message, 1, false);
      break;
    }
    default: {
      ExecuteBrowserCommandObserver* observer =
          new ExecuteBrowserCommandObserver(automation, reply_message);
      if (!observer->Register(command)) {
        delete observer;
        result = false;
      }
      break;
    }
  }
  return result;
}

void ExecuteBrowserCommandObserver::Observe(
    NotificationType type, const NotificationSource& source,
    const NotificationDetails& details) {
  if (type == notification_type_) {
    AutomationMsg_WindowExecuteCommand::WriteReplyParams(reply_message_,
                                                         true);
    automation_->Send(reply_message_);
    delete this;
  } else {
    NOTREACHED();
  }
}

ExecuteBrowserCommandObserver::ExecuteBrowserCommandObserver(
    AutomationProvider* automation, IPC::Message* reply_message)
    : automation_(automation),
      notification_type_(NotificationType::ALL),
      reply_message_(reply_message) {
}

bool ExecuteBrowserCommandObserver::Register(int command) {
  if (!GetNotificationType(command, &notification_type_))
    return false;
  registrar_.Add(this, notification_type_, NotificationService::AllSources());
  return true;
}

bool ExecuteBrowserCommandObserver::GetNotificationType(
    int command, NotificationType::Type* type) {
  if (!type)
    return false;
  bool found = false;
  for (unsigned int i = 0; i < arraysize(command_notifications); i++) {
    if (command_notifications[i].command == command) {
      *type = command_notifications[i].notification_type;
      found = true;
      break;
    }
  }
  return found;
}

FindInPageNotificationObserver::FindInPageNotificationObserver(
    AutomationProvider* automation, TabContents* parent_tab,
    IPC::Message* reply_message)
    : automation_(automation),
      active_match_ordinal_(-1),
      reply_message_(reply_message) {
  registrar_.Add(this, NotificationType::FIND_RESULT_AVAILABLE,
                 Source<TabContents>(parent_tab));
}

FindInPageNotificationObserver::~FindInPageNotificationObserver() {
}

void FindInPageNotificationObserver::Observe(
    NotificationType type, const NotificationSource& source,
    const NotificationDetails& details) {
  if (type == NotificationType::FIND_RESULT_AVAILABLE) {
    Details<FindNotificationDetails> find_details(details);
    if (find_details->request_id() == kFindInPageRequestId) {
      // We get multiple responses and one of those will contain the ordinal.
      // This message comes to us before the final update is sent.
      if (find_details->active_match_ordinal() > -1)
        active_match_ordinal_ = find_details->active_match_ordinal();
      if (find_details->final_update()) {
        if (reply_message_ != NULL) {
          AutomationMsg_FindInPage::WriteReplyParams(reply_message_,
              active_match_ordinal_, find_details->number_of_matches());
          automation_->Send(reply_message_);
          reply_message_ = NULL;
        } else {
          DLOG(WARNING) << "Multiple final Find messages observed.";
        }
      } else {
        DLOG(INFO) << "Ignoring, since we only care about the final message";
      }
    }
  } else {
    NOTREACHED();
  }
}

// static
const int FindInPageNotificationObserver::kFindInPageRequestId = -1;

DomOperationNotificationObserver::DomOperationNotificationObserver(
    AutomationProvider* automation)
    : automation_(automation) {
  registrar_.Add(this, NotificationType::DOM_OPERATION_RESPONSE,
                 NotificationService::AllSources());
}

DomOperationNotificationObserver::~DomOperationNotificationObserver() {
}

void DomOperationNotificationObserver::Observe(
    NotificationType type, const NotificationSource& source,
    const NotificationDetails& details) {
  if (NotificationType::DOM_OPERATION_RESPONSE == type) {
    Details<DomOperationNotificationDetails> dom_op_details(details);

    IPC::Message* reply_message = automation_->reply_message_release();
    if (reply_message) {
      AutomationMsg_DomOperation::WriteReplyParams(reply_message,
                                                   dom_op_details->json());
      automation_->Send(reply_message);
    }
  }
}

DocumentPrintedNotificationObserver::DocumentPrintedNotificationObserver(
    AutomationProvider* automation, IPC::Message* reply_message)
    : automation_(automation),
      success_(false),
      reply_message_(reply_message) {
  registrar_.Add(this, NotificationType::PRINT_JOB_EVENT,
                 NotificationService::AllSources());
}

DocumentPrintedNotificationObserver::~DocumentPrintedNotificationObserver() {
  DCHECK(reply_message_ != NULL);
  AutomationMsg_PrintNow::WriteReplyParams(reply_message_, success_);
  automation_->Send(reply_message_);
  automation_->RemoveNavigationStatusListener(this);
}

void DocumentPrintedNotificationObserver::Observe(
    NotificationType type, const NotificationSource& source,
    const NotificationDetails& details) {
  using namespace printing;
  DCHECK(type == NotificationType::PRINT_JOB_EVENT);
  switch (Details<JobEventDetails>(details)->type()) {
    case JobEventDetails::JOB_DONE: {
      // Succeeded.
      success_ = true;
      delete this;
      break;
    }
    case JobEventDetails::USER_INIT_CANCELED:
    case JobEventDetails::FAILED: {
      // Failed.
      delete this;
      break;
    }
    case JobEventDetails::NEW_DOC:
    case JobEventDetails::USER_INIT_DONE:
    case JobEventDetails::DEFAULT_INIT_DONE:
    case JobEventDetails::NEW_PAGE:
    case JobEventDetails::PAGE_DONE:
    case JobEventDetails::DOC_DONE:
    case JobEventDetails::ALL_PAGES_REQUESTED: {
      // Don't care.
      break;
    }
    default: {
      NOTREACHED();
      break;
    }
  }
}

MetricEventDurationObserver::MetricEventDurationObserver() {
  registrar_.Add(this, NotificationType::METRIC_EVENT_DURATION,
                 NotificationService::AllSources());
}

int MetricEventDurationObserver::GetEventDurationMs(
    const std::string& event_name) {
  EventDurationMap::const_iterator it = durations_.find(event_name);
  if (it == durations_.end())
    return -1;
  return it->second;
}

void MetricEventDurationObserver::Observe(NotificationType type,
    const NotificationSource& source, const NotificationDetails& details) {
  if (type != NotificationType::METRIC_EVENT_DURATION) {
    NOTREACHED();
    return;
  }
  MetricEventDurationDetails* metric_event_duration =
      Details<MetricEventDurationDetails>(details).ptr();
  durations_[metric_event_duration->event_name] =
      metric_event_duration->duration_ms;
}

PageTranslatedObserver::PageTranslatedObserver(AutomationProvider* automation,
                                               IPC::Message* reply_message,
                                               TabContents* tab_contents)
  : automation_(automation),
    reply_message_(reply_message) {
  registrar_.Add(this, NotificationType::PAGE_TRANSLATED,
                 Source<TabContents>(tab_contents));
}

void PageTranslatedObserver::Observe(NotificationType type,
                                     const NotificationSource& source,
                                     const NotificationDetails& details) {
  DCHECK(type == NotificationType::PAGE_TRANSLATED);
  AutomationJSONReply reply(automation_, reply_message_);

  PageTranslatedDetails* translated_details =
      Details<PageTranslatedDetails>(details).ptr();
  scoped_ptr<DictionaryValue> return_value(new DictionaryValue);
  return_value->SetBoolean(
      "translation_success",
      translated_details->error_type == TranslateErrors::NONE);
  reply.SendSuccess(return_value.get());
  delete this;
}

TabLanguageDeterminedObserver::TabLanguageDeterminedObserver(
    AutomationProvider* automation, IPC::Message* reply_message,
    TabContents* tab_contents, TranslateInfoBarDelegate* translate_bar)
  : automation_(automation),
    reply_message_(reply_message),
    tab_contents_(tab_contents),
    translate_bar_(translate_bar) {
  registrar_.Add(this, NotificationType::TAB_LANGUAGE_DETERMINED,
                 Source<TabContents>(tab_contents));
}

void TabLanguageDeterminedObserver::Observe(
    NotificationType type, const NotificationSource& source,
    const NotificationDetails& details) {
  DCHECK(type == NotificationType::TAB_LANGUAGE_DETERMINED);

  scoped_ptr<DictionaryValue> return_value(new DictionaryValue);
  return_value->SetBoolean("page_translated",
                           tab_contents_->language_state().IsPageTranslated());
  return_value->SetBoolean(
      "can_translate_page", TranslatePrefs::CanTranslate(
          automation_->profile()->GetPrefs(),
          tab_contents_->language_state().original_language(),
          tab_contents_->GetURL()));
  return_value->SetString(
      "original_language",
      tab_contents_->language_state().original_language());
  if (translate_bar_) {
    DictionaryValue* bar_info = new DictionaryValue;
    std::map<TranslateInfoBarDelegate::Type, std::string> type_to_string;
    type_to_string[TranslateInfoBarDelegate::BEFORE_TRANSLATE] =
        "BEFORE_TRANSLATE";
    type_to_string[TranslateInfoBarDelegate::TRANSLATING] =
        "TRANSLATING";
    type_to_string[TranslateInfoBarDelegate::AFTER_TRANSLATE] =
        "AFTER_TRANSLATE";
    type_to_string[TranslateInfoBarDelegate::TRANSLATION_ERROR] =
        "TRANSLATION_ERROR";

    bar_info->SetBoolean("always_translate_lang_button_showing",
                         translate_bar_->ShouldShowAlwaysTranslateButton());
    bar_info->SetBoolean("never_translate_lang_button_showing",
                         translate_bar_->ShouldShowNeverTranslateButton());
    bar_info->SetString("bar_state", type_to_string[translate_bar_->type()]);
    bar_info->SetString("target_lang_code",
                        translate_bar_->GetTargetLanguageCode());
    bar_info->SetString("original_lang_code",
                        translate_bar_->GetOriginalLanguageCode());
    return_value->Set("translate_bar", bar_info);
  }
  AutomationJSONReply(automation_, reply_message_)
      .SendSuccess(return_value.get());
  delete this;
}

InfoBarCountObserver::InfoBarCountObserver(AutomationProvider* automation,
                                           IPC::Message* reply_message,
                                           TabContents* tab_contents,
                                           int target_count)
    : automation_(automation),
      reply_message_(reply_message),
      tab_contents_(tab_contents),
      target_count_(target_count) {
  Source<TabContents> source(tab_contents);
  registrar_.Add(this, NotificationType::TAB_CONTENTS_INFOBAR_ADDED, source);
  registrar_.Add(this, NotificationType::TAB_CONTENTS_INFOBAR_REMOVED, source);
  CheckCount();
}

void InfoBarCountObserver::Observe(NotificationType type,
                                   const NotificationSource& source,
                                   const NotificationDetails& details) {
  DCHECK(type == NotificationType::TAB_CONTENTS_INFOBAR_ADDED ||
         type == NotificationType::TAB_CONTENTS_INFOBAR_REMOVED);
  CheckCount();
}

void InfoBarCountObserver::CheckCount() {
  if (tab_contents_->infobar_delegate_count() != target_count_)
    return;

  AutomationMsg_WaitForInfoBarCount::WriteReplyParams(reply_message_, true);
  automation_->Send(reply_message_);
  delete this;
}

#if defined(OS_CHROMEOS)
LoginManagerObserver::LoginManagerObserver(
    AutomationProvider* automation,
    IPC::Message* reply_message)
    : automation_(automation),
      reply_message_(reply_message) {

  registrar_.Add(this, NotificationType::LOGIN_AUTHENTICATION,
                 NotificationService::AllSources());
}

void LoginManagerObserver::Observe(NotificationType type,
                                   const NotificationSource& source,
                                   const NotificationDetails& details) {
  DCHECK(type == NotificationType::LOGIN_AUTHENTICATION);
  Details<AuthenticationNotificationDetails> auth_details(details);
  AutomationMsg_LoginWithUserAndPass::WriteReplyParams(reply_message_,
      auth_details->success());
  automation_->Send(reply_message_);
  delete this;
}
#endif

AutomationProviderBookmarkModelObserver::
AutomationProviderBookmarkModelObserver(
    AutomationProvider* provider,
    IPC::Message* reply_message,
    BookmarkModel* model) {
  automation_provider_ = provider;
  reply_message_ = reply_message;
  model_ = model;
  model_->AddObserver(this);
}

AutomationProviderBookmarkModelObserver::
~AutomationProviderBookmarkModelObserver() {
  model_->RemoveObserver(this);
}

void AutomationProviderBookmarkModelObserver::ReplyAndDelete(bool success) {
  AutomationMsg_WaitForBookmarkModelToLoad::WriteReplyParams(
      reply_message_, success);
  automation_provider_->Send(reply_message_);
  delete this;
}

void AutomationProviderDownloadItemObserver::OnDownloadFileCompleted(
    DownloadItem* download) {
  download->RemoveObserver(this);
  if (--downloads_ == 0) {
    AutomationJSONReply(provider_, reply_message_).SendSuccess(NULL);
    delete this;
  }
}

void AutomationProviderDownloadUpdatedObserver::OnDownloadUpdated(
    DownloadItem* download) {
  // If this observer is watching for open, only send the reply if the download
  // has been auto-opened.
  if (wait_for_open_ && !download->auto_opened())
    return;

  download->RemoveObserver(this);
  scoped_ptr<DictionaryValue> return_value(
      provider_->GetDictionaryFromDownloadItem(download));
  AutomationJSONReply(provider_, reply_message_).SendSuccess(
      return_value.get());
  delete this;
}

void AutomationProviderDownloadUpdatedObserver::OnDownloadOpened(
    DownloadItem* download) {
  download->RemoveObserver(this);
  scoped_ptr<DictionaryValue> return_value(
      provider_->GetDictionaryFromDownloadItem(download));
  AutomationJSONReply(provider_, reply_message_).SendSuccess(
      return_value.get());
  delete this;
}

void AutomationProviderDownloadModelChangedObserver::ModelChanged() {
  AutomationJSONReply(provider_, reply_message_).SendSuccess(NULL);
  download_manager_->RemoveObserver(this);
  delete this;
}

void AutomationProviderSearchEngineObserver::OnTemplateURLModelChanged() {
  TemplateURLModel* url_model = provider_->profile()->GetTemplateURLModel();
  scoped_ptr<DictionaryValue> return_value(new DictionaryValue);
  return_value->Set("search_engines",
                    provider_->ExtractSearchEngineInfo(url_model));

  url_model->RemoveObserver(this);
  AutomationJSONReply(provider_, reply_message_).SendSuccess(
      return_value.get());
  delete this;
}

void AutomationProviderHistoryObserver::HistoryQueryComplete(
    HistoryService::Handle request_handle,
    history::QueryResults* results) {
  scoped_ptr<DictionaryValue> return_value(new DictionaryValue);

  ListValue* history_list = new ListValue;
  for (size_t i = 0; i < results->size(); ++i) {
    DictionaryValue* page_value = new DictionaryValue;
    history::URLResult const &page = (*results)[i];
    page_value->SetString("title", page.title());
    page_value->SetString("url", page.url().spec());
    page_value->SetReal("time",
                        static_cast<double>(page.visit_time().ToDoubleT()));
    page_value->SetString("snippet", page.snippet().text());
    page_value->SetBoolean(
        "starred",
        provider_->profile()->GetBookmarkModel()->IsBookmarked(page.url()));
    history_list->Append(page_value);
  }

  return_value->Set("history", history_list);
  // Return history info.
  AutomationJSONReply reply(provider_, reply_message_);
  reply.SendSuccess(return_value.get());
  delete this;
}

void AutomationProviderImportSettingsObserver::ImportEnded() {
  // Send back an empty success message.
  AutomationJSONReply(provider_, reply_message_).SendSuccess(NULL);
  delete this;
}

void AutomationProviderGetPasswordsObserver::OnPasswordStoreRequestDone(
    int handle, const std::vector<webkit_glue::PasswordForm*>& result) {
  scoped_ptr<DictionaryValue> return_value(new DictionaryValue);

  ListValue* passwords = new ListValue;
  for (std::vector<webkit_glue::PasswordForm*>::const_iterator it =
          result.begin(); it != result.end(); ++it) {
    DictionaryValue* password_val = new DictionaryValue;
    webkit_glue::PasswordForm* password_form = *it;
    password_val->SetString("username_value", password_form->username_value);
    password_val->SetString("password_value", password_form->password_value);
    password_val->SetString("signon_realm", password_form->signon_realm);
    password_val->SetReal(
        "time", static_cast<double>(password_form->date_created.ToDoubleT()));
    password_val->SetString("origin_url", password_form->origin.spec());
    password_val->SetString("username_element",
                            password_form->username_element);
    password_val->SetString("password_element",
                            password_form->password_element);
    password_val->SetString("submit_element",
                                     password_form->submit_element);
    password_val->SetString("action_target", password_form->action.spec());
    password_val->SetBoolean("blacklist", password_form->blacklisted_by_user);
    passwords->Append(password_val);
  }

  return_value->Set("passwords", passwords);
  AutomationJSONReply(provider_, reply_message_).SendSuccess(
      return_value.get());
  delete this;
}

void AutomationProviderBrowsingDataObserver::OnBrowsingDataRemoverDone() {
  // Send back an empty success message
  AutomationJSONReply(provider_, reply_message_).SendSuccess(NULL);
  delete this;
}

OmniboxAcceptNotificationObserver::OmniboxAcceptNotificationObserver(
    NavigationController* controller,
    AutomationProvider* automation,
    IPC::Message* reply_message)
  : automation_(automation),
    reply_message_(reply_message),
    controller_(controller) {
  Source<NavigationController> source(controller_);
  registrar_.Add(this, NotificationType::LOAD_STOP, source);
  // Pages requiring auth don't send LOAD_STOP.
  registrar_.Add(this, NotificationType::AUTH_NEEDED, source);
}

OmniboxAcceptNotificationObserver::~OmniboxAcceptNotificationObserver() {
  automation_->RemoveNavigationStatusListener(this);
}

void OmniboxAcceptNotificationObserver::Observe(
    NotificationType type,
    const NotificationSource& source,
    const NotificationDetails& details) {
  if (type == NotificationType::LOAD_STOP ||
      type == NotificationType::AUTH_NEEDED) {
    AutomationJSONReply(automation_, reply_message_).SendSuccess(NULL);
    delete this;
  } else {
    NOTREACHED();
  }
}

SavePackageNotificationObserver::SavePackageNotificationObserver(
    SavePackage* save_package,
    AutomationProvider* automation,
    IPC::Message* reply_message) : automation_(automation),
                                   reply_message_(reply_message) {
  Source<SavePackage> source(save_package);
  registrar_.Add(this, NotificationType::SAVE_PACKAGE_SUCCESSFULLY_FINISHED,
                 source);
}

void SavePackageNotificationObserver::Observe(
    NotificationType type,
    const NotificationSource& source,
    const NotificationDetails& details) {
  if (type == NotificationType::SAVE_PACKAGE_SUCCESSFULLY_FINISHED) {
    AutomationJSONReply(automation_, reply_message_).SendSuccess(NULL);
    delete this;
  } else {
    NOTREACHED();
  }
}

AutocompleteEditFocusedObserver::AutocompleteEditFocusedObserver(
    AutomationProvider* automation,
    AutocompleteEditModel* autocomplete_edit,
    IPC::Message* reply_message)
    : automation_(automation),
      reply_message_(reply_message),
      autocomplete_edit_model_(autocomplete_edit) {
  Source<AutocompleteEditModel> source(autocomplete_edit);
  registrar_.Add(this, NotificationType::AUTOCOMPLETE_EDIT_FOCUSED, source);
}

void AutocompleteEditFocusedObserver::Observe(
    NotificationType type,
    const NotificationSource& source,
    const NotificationDetails& details) {
  DCHECK(type == NotificationType::AUTOCOMPLETE_EDIT_FOCUSED);
  AutomationMsg_WaitForAutocompleteEditFocus::WriteReplyParams(
      reply_message_, true);
  automation_->Send(reply_message_);
  delete this;
}
