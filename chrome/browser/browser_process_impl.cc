// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/browser_process_impl.h"

#include <map>

#include "app/clipboard/clipboard.h"
#include "app/l10n_util.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/path_service.h"
#include "base/thread.h"
#include "base/waitable_event.h"
#include "chrome/browser/appcache/chrome_appcache_service.h"
#include "chrome/browser/browser_child_process_host.h"
#include "chrome/browser/browser_list.h"
#include "chrome/browser/browser_main.h"
#include "chrome/browser/browser_process_sub_thread.h"
#include "chrome/browser/browser_trial.h"
#include "chrome/browser/chrome_thread.h"
#include "chrome/browser/debugger/debugger_wrapper.h"
#include "chrome/browser/debugger/devtools_manager.h"
#include "chrome/browser/download/download_file_manager.h"
#include "chrome/browser/download/save_file_manager.h"
#include "chrome/browser/first_run.h"
#include "chrome/browser/google_url_tracker.h"
#include "chrome/browser/icon_manager.h"
#include "chrome/browser/in_process_webkit/dom_storage_context.h"
#include "chrome/browser/intranet_redirect_detector.h"
#include "chrome/browser/io_thread.h"
#include "chrome/browser/metrics/metrics_service.h"
#include "chrome/browser/net/predictor_api.h"
#include "chrome/browser/net/sdch_dictionary_fetcher.h"
#include "chrome/browser/net/sqlite_persistent_cookie_store.h"
#include "chrome/browser/notifications/notification_ui_manager.h"
#include "chrome/browser/plugin_service.h"
#include "chrome/browser/pref_service.h"
#include "chrome/browser/printing/print_job_manager.h"
#include "chrome/browser/profile_manager.h"
#include "chrome/browser/renderer_host/render_process_host.h"
#include "chrome/browser/renderer_host/resource_dispatcher_host.h"
#include "chrome/browser/safe_browsing/safe_browsing_service.h"
#include "chrome/browser/status_icons/status_tray_manager.h"
#include "chrome/browser/tab_closeable_state_watcher.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension_resource.h"
#include "chrome/common/extensions/extension_l10n_util.h"
#include "chrome/common/json_pref_store.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "chrome/installer/util/google_update_constants.h"
#include "ipc/ipc_logging.h"
#include "webkit/database/database_tracker.h"

#if defined(OS_WIN)
#include "views/focus/view_storage.h"
#endif

#if defined(IPC_MESSAGE_LOG_ENABLED)
#include "chrome/common/plugin_messages.h"
#include "chrome/common/render_messages.h"
#endif

#if (defined(OS_WIN) || defined(OS_LINUX)) && !defined(OS_CHROMEOS)
// How often to check if the persistent instance of Chrome needs to restart
// to install an update.
static const int kUpdateCheckIntervalHours = 6;
#endif

BrowserProcessImpl::BrowserProcessImpl(const CommandLine& command_line)
    : created_resource_dispatcher_host_(false),
      created_metrics_service_(false),
      created_io_thread_(false),
      created_file_thread_(false),
      created_db_thread_(false),
      created_process_launcher_thread_(false),
      created_cache_thread_(false),
      created_profile_manager_(false),
      created_local_state_(false),
      created_icon_manager_(false),
      created_debugger_wrapper_(false),
      created_devtools_manager_(false),
      created_notification_ui_manager_(false),
      module_ref_count_(0),
      did_start_(false),
      checked_for_new_frames_(false),
      using_new_frames_(false),
      have_inspector_files_(true) {
  g_browser_process = this;
  clipboard_.reset(new Clipboard);
  main_notification_service_.reset(new NotificationService);

  // Must be created after the NotificationService.
  print_job_manager_.reset(new printing::PrintJobManager);

  shutdown_event_.reset(new base::WaitableEvent(true, false));
}

BrowserProcessImpl::~BrowserProcessImpl() {
  FilePath profile_path;
  bool clear_local_state_on_exit;

  // Store the profile path for clearing local state data on exit.
  clear_local_state_on_exit = ShouldClearLocalState(&profile_path);

  // Delete the AutomationProviderList before NotificationService,
  // since it may try to unregister notifications
  // Both NotificationService and AutomationProvider are singleton instances in
  // the BrowserProcess. Since AutomationProvider may have some active
  // notification observers, it is essential that it gets destroyed before the
  // NotificationService. NotificationService won't be destroyed until after
  // this destructor is run.
  automation_provider_list_.reset();

  // We need to shutdown the SdchDictionaryFetcher as it regularly holds
  // a pointer to a URLFetcher, and that URLFetcher (upon destruction) will do
  // a PostDelayedTask onto the IO thread.  This shutdown call will both discard
  // any pending URLFetchers, and avoid creating any more.
  SdchDictionaryFetcher::Shutdown();

  // We need to destroy the MetricsService, GoogleURLTracker, and
  // IntranetRedirectDetector before the io_thread_ gets destroyed, since their
  // destructors can call the URLFetcher destructor, which does a
  // PostDelayedTask operation on the IO thread.  (The IO thread will handle
  // that URLFetcher operation before going away.)
  metrics_service_.reset();
  google_url_tracker_.reset();
  intranet_redirect_detector_.reset();

  // Need to clear profiles (download managers) before the io_thread_.
  profile_manager_.reset();

  // Debugger must be cleaned up before IO thread and NotificationService.
  debugger_wrapper_ = NULL;

  if (resource_dispatcher_host_.get()) {
    // Need to tell Safe Browsing Service that the IO thread is going away
    // since it cached a pointer to it.
    if (resource_dispatcher_host()->safe_browsing_service())
      resource_dispatcher_host()->safe_browsing_service()->ShutDown();

    // Cancel pending requests and prevent new requests.
    resource_dispatcher_host()->Shutdown();
  }

#if defined(USE_X11)
  // The IO thread must outlive the BACKGROUND_X11 thread.
  background_x11_thread_.reset();
#endif

  // Need to stop io_thread_ before resource_dispatcher_host_, since
  // io_thread_ may still deref ResourceDispatcherHost and handle resource
  // request before going away.
  io_thread_.reset();

  // The IO thread was the only user of this thread.
  cache_thread_.reset();

  // Stop the process launcher thread after the IO thread, in case the IO thread
  // posted a task to terminate a process on the process launcher thread.
  process_launcher_thread_.reset();

  // Clean up state that lives on the file_thread_ before it goes away.
  if (resource_dispatcher_host_.get()) {
    resource_dispatcher_host()->download_file_manager()->Shutdown();
    resource_dispatcher_host()->save_file_manager()->Shutdown();
  }

  // Need to stop the file_thread_ here to force it to process messages in its
  // message loop from the previous call to shutdown the DownloadFileManager,
  // SaveFileManager and SessionService.
  file_thread_.reset();

  // With the file_thread_ flushed, we can release any icon resources.
  icon_manager_.reset();

  // Need to destroy ResourceDispatcherHost before PluginService and
  // SafeBrowsingService, since it caches a pointer to it. This also
  // causes the webkit thread to terminate.
  resource_dispatcher_host_.reset();

  // Wait for the pending print jobs to finish.
  print_job_manager_->OnQuit();
  print_job_manager_.reset();

  // Destroy TabCloseableStateWatcher before NotificationService since the
  // former registers for notifications.
  tab_closeable_state_watcher_.reset();

  // Now OK to destroy NotificationService.
  main_notification_service_.reset();

  // Prior to clearing local state, we want to complete tasks pending
  // on the db thread too.
  db_thread_.reset();

  // At this point, no render process exist and the file, io, db, and
  // webkit threads in this process have all terminated, so it's safe
  // to access local state data such as cookies, database, or local storage.
  if (clear_local_state_on_exit)
    ClearLocalState(profile_path);

  g_browser_process = NULL;
}

// Send a QuitTask to the given MessageLoop.
static void PostQuit(MessageLoop* message_loop) {
  message_loop->PostTask(FROM_HERE, new MessageLoop::QuitTask());
}

unsigned int BrowserProcessImpl::AddRefModule() {
  DCHECK(CalledOnValidThread());
  did_start_ = true;
  module_ref_count_++;
  return module_ref_count_;
}

unsigned int BrowserProcessImpl::ReleaseModule() {
  DCHECK(CalledOnValidThread());
  DCHECK_NE(0u, module_ref_count_);
  module_ref_count_--;
  if (0 == module_ref_count_) {
    MessageLoop::current()->PostTask(
        FROM_HERE, NewRunnableFunction(DidEndMainMessageLoop));
    MessageLoop::current()->Quit();
  }
  return module_ref_count_;
}

void BrowserProcessImpl::EndSession() {
#if defined(OS_WIN)
  // Notify we are going away.
  ::SetEvent(shutdown_event_->handle());
#endif

  // Mark all the profiles as clean.
  ProfileManager* pm = profile_manager();
  for (ProfileManager::const_iterator i = pm->begin(); i != pm->end(); ++i)
    (*i)->MarkAsCleanShutdown();

  // Tell the metrics service it was cleanly shutdown.
  MetricsService* metrics = g_browser_process->metrics_service();
  if (metrics && local_state()) {
    metrics->RecordCleanShutdown();

    metrics->RecordStartOfSessionEnd();

    // MetricsService lazily writes to prefs, force it to write now.
    local_state()->SavePersistentPrefs();
  }

  // We must write that the profile and metrics service shutdown cleanly,
  // otherwise on startup we'll think we crashed. So we block until done and
  // then proceed with normal shutdown.
  g_browser_process->file_thread()->message_loop()->PostTask(FROM_HERE,
      NewRunnableFunction(PostQuit, MessageLoop::current()));
  MessageLoop::current()->Run();
}

printing::PrintJobManager* BrowserProcessImpl::print_job_manager() {
  // TODO(abarth): DCHECK(CalledOnValidThread());
  // http://code.google.com/p/chromium/issues/detail?id=6828
  // print_job_manager_ is initialized in the constructor and destroyed in the
  // destructor, so it should always be valid.
  DCHECK(print_job_manager_.get());
  return print_job_manager_.get();
}

void BrowserProcessImpl::ClearLocalState(const FilePath& profile_path) {
  SQLitePersistentCookieStore::ClearLocalState(profile_path.Append(
      chrome::kCookieFilename));
  DOMStorageContext::ClearLocalState(profile_path, chrome::kExtensionScheme);
  webkit_database::DatabaseTracker::ClearLocalState(profile_path);
  ChromeAppCacheService::ClearLocalState(profile_path);
}

bool BrowserProcessImpl::ShouldClearLocalState(FilePath* profile_path) {
  FilePath user_data_dir;
  Profile* profile;

  // Check for the existence of a profile manager. When quitting early,
  // e.g. because another chrome instance is running, or when invoked with
  // options such as --uninstall or --try-chrome-again=0, the profile manager
  // does not exist yet.
  if (!profile_manager_.get())
    return false;

  PathService::Get(chrome::DIR_USER_DATA, &user_data_dir);
  profile = profile_manager_->GetDefaultProfile(user_data_dir);
  if (!profile)
    return false;
  *profile_path = profile->GetPath();
  return profile->GetPrefs()->GetBoolean(prefs::kClearSiteDataOnExit);
}

void BrowserProcessImpl::CreateResourceDispatcherHost() {
  DCHECK(!created_resource_dispatcher_host_ &&
         resource_dispatcher_host_.get() == NULL);
  created_resource_dispatcher_host_ = true;

  resource_dispatcher_host_.reset(new ResourceDispatcherHost());
  resource_dispatcher_host_->Initialize();
}

void BrowserProcessImpl::CreateMetricsService() {
  DCHECK(!created_metrics_service_ && metrics_service_.get() == NULL);
  created_metrics_service_ = true;

  metrics_service_.reset(new MetricsService);
}

void BrowserProcessImpl::CreateIOThread() {
  DCHECK(!created_io_thread_ && io_thread_.get() == NULL);
  created_io_thread_ = true;

  // Prior to starting the io thread, we create the plugin service as
  // it is predominantly used from the io thread, but must be created
  // on the main thread. The service ctor is inexpensive and does not
  // invoke the io_thread() accessor.
  PluginService::GetInstance();

#if defined(USE_X11)
  // The lifetime of the BACKGROUND_X11 thread is a subset of the IO thread so
  // we start it now.
  scoped_ptr<base::Thread> background_x11_thread(
      new BrowserProcessSubThread(ChromeThread::BACKGROUND_X11));
  if (!background_x11_thread->Start())
    return;
  background_x11_thread_.swap(background_x11_thread);
#endif

  scoped_ptr<IOThread> thread(new IOThread);
  base::Thread::Options options;
  options.message_loop_type = MessageLoop::TYPE_IO;
  if (!thread->StartWithOptions(options))
    return;
  io_thread_.swap(thread);
}

void BrowserProcessImpl::CreateFileThread() {
  DCHECK(!created_file_thread_ && file_thread_.get() == NULL);
  created_file_thread_ = true;

  scoped_ptr<base::Thread> thread(
      new BrowserProcessSubThread(ChromeThread::FILE));
  base::Thread::Options options;
#if defined(OS_WIN)
  // On Windows, the FILE thread needs to be have a UI message loop which pumps
  // messages in such a way that Google Update can communicate back to us.
  options.message_loop_type = MessageLoop::TYPE_UI;
#else
  options.message_loop_type = MessageLoop::TYPE_IO;
#endif
  if (!thread->StartWithOptions(options))
    return;
  file_thread_.swap(thread);

  // ExtensionResource is in chrome/common, so it cannot depend on
  // chrome/browser, which means it cannot lookup what the File thread is.
  // We therefore store the thread ID from here so we can validate the proper
  // thread usage in the ExtensionResource class.
  ExtensionResource::set_file_thread_id(file_thread_->thread_id());
}

void BrowserProcessImpl::CreateDBThread() {
  DCHECK(!created_db_thread_ && db_thread_.get() == NULL);
  created_db_thread_ = true;

  scoped_ptr<base::Thread> thread(
      new BrowserProcessSubThread(ChromeThread::DB));
  if (!thread->Start())
    return;
  db_thread_.swap(thread);
}

void BrowserProcessImpl::CreateProcessLauncherThread() {
  DCHECK(!created_process_launcher_thread_ && !process_launcher_thread_.get());
  created_process_launcher_thread_ = true;

  scoped_ptr<base::Thread> thread(
      new BrowserProcessSubThread(ChromeThread::PROCESS_LAUNCHER));
  if (!thread->Start())
    return;
  process_launcher_thread_.swap(thread);
}

void BrowserProcessImpl::CreateCacheThread() {
  DCHECK(!created_cache_thread_ && !cache_thread_.get());
  created_cache_thread_ = true;

  scoped_ptr<base::Thread> thread(
      new BrowserProcessSubThread(ChromeThread::CACHE));
  base::Thread::Options options;
  options.message_loop_type = MessageLoop::TYPE_IO;
  if (!thread->StartWithOptions(options))
    return;
  cache_thread_.swap(thread);
}

void BrowserProcessImpl::CreateProfileManager() {
  DCHECK(!created_profile_manager_ && profile_manager_.get() == NULL);
  created_profile_manager_ = true;

  profile_manager_.reset(new ProfileManager());
}

void BrowserProcessImpl::CreateLocalState() {
  DCHECK(!created_local_state_ && local_state_.get() == NULL);
  created_local_state_ = true;

  FilePath local_state_path;
  PathService::Get(chrome::FILE_LOCAL_STATE, &local_state_path);
  local_state_.reset(PrefService::CreatePrefService(local_state_path));
  }

void BrowserProcessImpl::CreateIconManager() {
  DCHECK(!created_icon_manager_ && icon_manager_.get() == NULL);
  created_icon_manager_ = true;
  icon_manager_.reset(new IconManager);
}

void BrowserProcessImpl::CreateDebuggerWrapper(int port, bool useHttp) {
  DCHECK(debugger_wrapper_.get() == NULL);
  created_debugger_wrapper_ = true;

  debugger_wrapper_ = new DebuggerWrapper(port, useHttp);
}

void BrowserProcessImpl::CreateDevToolsManager() {
  DCHECK(devtools_manager_.get() == NULL);
  created_devtools_manager_ = true;
  devtools_manager_ = new DevToolsManager();
}

void BrowserProcessImpl::CreateGoogleURLTracker() {
  DCHECK(google_url_tracker_.get() == NULL);
  scoped_ptr<GoogleURLTracker> google_url_tracker(new GoogleURLTracker);
  google_url_tracker_.swap(google_url_tracker);
}

void BrowserProcessImpl::CreateIntranetRedirectDetector() {
  DCHECK(intranet_redirect_detector_.get() == NULL);
  scoped_ptr<IntranetRedirectDetector> intranet_redirect_detector(
      new IntranetRedirectDetector);
  intranet_redirect_detector_.swap(intranet_redirect_detector);
}

void BrowserProcessImpl::CreateNotificationUIManager() {
  DCHECK(notification_ui_manager_.get() == NULL);
  notification_ui_manager_.reset(NotificationUIManager::Create());
  created_notification_ui_manager_ = true;
}

void BrowserProcessImpl::SetApplicationLocale(const std::string& locale) {
  locale_ = locale;
  extension_l10n_util::SetProcessLocale(locale);
}

void BrowserProcessImpl::CreateStatusTrayManager() {
  DCHECK(status_tray_manager_.get() == NULL);
  status_tray_manager_.reset(new StatusTrayManager());
}

void BrowserProcessImpl::CreateTabCloseableStateWatcher() {
  DCHECK(tab_closeable_state_watcher_.get() == NULL);
  tab_closeable_state_watcher_.reset(TabCloseableStateWatcher::Create());
}

// The BrowserProcess object must outlive the file thread so we use traits
// which don't do any management.
DISABLE_RUNNABLE_METHOD_REFCOUNT(BrowserProcessImpl);

void BrowserProcessImpl::CheckForInspectorFiles() {
  file_thread()->message_loop()->PostTask
      (FROM_HERE,
       NewRunnableMethod(this, &BrowserProcessImpl::DoInspectorFilesCheck));
}

#if (defined(OS_WIN) || defined(OS_LINUX)) && !defined(OS_CHROMEOS)
void BrowserProcessImpl::StartAutoupdateTimer() {
  autoupdate_timer_.Start(
      base::TimeDelta::FromHours(kUpdateCheckIntervalHours),
      this,
      &BrowserProcessImpl::OnAutoupdateTimer);
}
#endif

#if defined(IPC_MESSAGE_LOG_ENABLED)

void BrowserProcessImpl::SetIPCLoggingEnabled(bool enable) {
  // First enable myself.
  if (enable)
    IPC::Logging::current()->Enable();
  else
    IPC::Logging::current()->Disable();

  // Now tell subprocesses.  Messages to ChildProcess-derived
  // processes must be done on the IO thread.
  io_thread()->message_loop()->PostTask
      (FROM_HERE,
       NewRunnableMethod(
           this,
           &BrowserProcessImpl::SetIPCLoggingEnabledForChildProcesses,
           enable));

  // Finally, tell the renderers which don't derive from ChildProcess.
  // Messages to the renderers must be done on the UI (main) thread.
  for (RenderProcessHost::iterator i(RenderProcessHost::AllHostsIterator());
       !i.IsAtEnd(); i.Advance())
    i.GetCurrentValue()->Send(new ViewMsg_SetIPCLoggingEnabled(enable));
}

// Helper for SetIPCLoggingEnabled.
void BrowserProcessImpl::SetIPCLoggingEnabledForChildProcesses(bool enabled) {
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::IO));

  BrowserChildProcessHost::Iterator i;  // default constr references a singleton
  while (!i.Done()) {
    i->Send(new PluginProcessMsg_SetIPCLoggingEnabled(enabled));
    ++i;
  }
}

#endif  // IPC_MESSAGE_LOG_ENABLED

void BrowserProcessImpl::DoInspectorFilesCheck() {
  // Runs on FILE thread.
  DCHECK(file_thread_->message_loop() == MessageLoop::current());
  bool result = false;

  FilePath inspector_dir;
  if (PathService::Get(chrome::DIR_INSPECTOR, &inspector_dir)) {
    result = file_util::PathExists(inspector_dir);
  }

  have_inspector_files_ = result;
}

// Mac is currently not supported.
#if (defined(OS_WIN) || defined(OS_LINUX)) && !defined(OS_CHROMEOS)

bool BrowserProcessImpl::CanAutorestartForUpdate() const {
  // Check if browser is in the background and if it needs to be restarted to
  // apply a pending update.
  return BrowserList::size() == 0 && !BrowserList::WillKeepAlive() &&
         Upgrade::IsUpdatePendingRestart();
}

// Switches enumerated here will be removed when a background instance of
// Chrome restarts itself. If your key is designed to only be used once,
// or if it does not make sense when restarting a background instance to
// pick up an automatic update, be sure to add it to this list.
const char* const kSwitchesToRemoveOnAutorestart[] = {
    switches::kApp,
    switches::kFirstRun,
    switches::kImport,
    switches::kImportFromFile,
    switches::kMakeDefaultBrowser
};

void BrowserProcessImpl::RestartPersistentInstance() {
  CommandLine* old_cl = CommandLine::ForCurrentProcess();
  scoped_ptr<CommandLine> new_cl(new CommandLine(old_cl->GetProgram()));

  std::map<std::string, CommandLine::StringType> switches =
      old_cl->GetSwitches();

  // Remove the keys that we shouldn't pass through during restart.
  for (size_t i = 0; i < arraysize(kSwitchesToRemoveOnAutorestart); i++) {
    switches.erase(kSwitchesToRemoveOnAutorestart[i]);
  }

  // Append the rest of the switches (along with their values, if any)
  // to the new command line
  for (std::map<std::string, CommandLine::StringType>::const_iterator i =
      switches.begin(); i != switches.end(); ++i) {
      CommandLine::StringType switch_value = i->second;
      if (switch_value.length() > 0) {
        new_cl->AppendSwitchWithValue(i->first, i->second);
      } else {
        new_cl->AppendSwitch(i->first);
      }
  }

  if (!new_cl->HasSwitch(switches::kRestoreBackgroundContents))
    new_cl->AppendSwitch(switches::kRestoreBackgroundContents);

  DLOG(WARNING) << "Shutting down current instance of the browser.";
  BrowserList::CloseAllBrowsersAndExit();

  // Transfer ownership to Upgrade.
  Upgrade::SetNewCommandLine(new_cl.release());
}

void BrowserProcessImpl::OnAutoupdateTimer() {
  if (CanAutorestartForUpdate()) {
    DLOG(WARNING) << "Detected update.  Restarting browser.";
    RestartPersistentInstance();
  }
}

#endif  // (defined(OS_WIN) || defined(OS_LINUX)) && !defined(OS_CHROMEOS)
