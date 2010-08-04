// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/first_run.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <set>
#include <sstream>

// TODO(port): trim this include list once first run has been refactored fully.
#include "app/app_switches.h"
#include "app/l10n_util.h"
#include "app/l10n_util_win.h"
#include "app/resource_bundle.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/object_watcher.h"
#include "base/path_service.h"
#include "base/process.h"
#include "base/process_util.h"
#include "base/registry.h"
#include "base/scoped_comptr_win.h"
#include "base/string_util.h"
#include "base/win_util.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/notification_registrar.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/notification_type.h"
#include "chrome/common/pref_names.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/extensions_service.h"
#include "chrome/browser/extensions/extension_updater.h"
#include "chrome/browser/hang_monitor/hung_window_detector.h"
#include "chrome/browser/importer/importer.h"
#include "chrome/browser/importer/importer_data_types.h"
#include "chrome/browser/metrics/user_metrics.h"
#include "chrome/browser/pref_service.h"
#include "chrome/browser/process_singleton.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/profile_manager.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/views/first_run_search_engine_view.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/result_codes.h"
#include "chrome/installer/util/browser_distribution.h"
#include "chrome/installer/util/google_update_constants.h"
#include "chrome/installer/util/google_update_settings.h"
#include "chrome/installer/util/install_util.h"
#include "chrome/installer/util/master_preferences.h"
#include "chrome/installer/util/shell_util.h"
#include "chrome/installer/util/util_constants.h"
#include "google_update_idl.h"
#include "grit/app_resources.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "grit/theme_resources.h"
#include "views/background.h"
#include "views/controls/button/image_button.h"
#include "views/controls/button/native_button.h"
#include "views/controls/button/radio_button.h"
#include "views/controls/image_view.h"
#include "views/controls/label.h"
#include "views/controls/link.h"
#include "views/focus/accelerator_handler.h"
#include "views/grid_layout.h"
#include "views/standard_layout.h"
#include "views/widget/root_view.h"
#include "views/widget/widget_win.h"
#include "views/window/window.h"
#include "views/window/window_delegate.h"
#include "views/window/window_win.h"

namespace {

bool GetNewerChromeFile(FilePath* path) {
  if (!PathService::Get(base::DIR_EXE, path))
    return false;
  *path = path->Append(installer_util::kChromeNewExe);
  return true;
}

bool GetBackupChromeFile(std::wstring* path) {
  if (!PathService::Get(base::DIR_EXE, path))
    return false;
  file_util::AppendToPath(path, installer_util::kChromeOldExe);
  return true;
}

FilePath GetDefaultPrefFilePath(bool create_profile_dir,
                                const FilePath& user_data_dir) {
  FilePath default_pref_dir =
      ProfileManager::GetDefaultProfileDir(user_data_dir);
  if (create_profile_dir) {
    if (!file_util::PathExists(default_pref_dir)) {
      if (!file_util::CreateDirectory(default_pref_dir))
        return FilePath();
    }
  }
  return ProfileManager::GetProfilePrefsPath(default_pref_dir);
}

bool InvokeGoogleUpdateForRename() {
  ScopedComPtr<IProcessLauncher> ipl;
  if (!FAILED(ipl.CreateInstance(__uuidof(ProcessLauncherClass)))) {
    ULONG_PTR phandle = NULL;
    DWORD id = GetCurrentProcessId();
    BrowserDistribution* dist = BrowserDistribution::GetDistribution();
    if (!FAILED(ipl->LaunchCmdElevated(dist->GetAppGuid().c_str(),
                                       google_update::kRegRenameCmdField,
                                       id, &phandle))) {
      HANDLE handle = HANDLE(phandle);
      DWORD exit_code;
      ::GetExitCodeProcess(handle, &exit_code);
      ::CloseHandle(handle);
      if (exit_code == installer_util::RENAME_SUCCESSFUL)
        return true;
    }
  }
  return false;
}

bool LaunchSetupWithParam(const std::string& param, const std::wstring& value,
                          int* ret_code) {
  FilePath exe_path;
  if (!PathService::Get(base::DIR_MODULE, &exe_path))
    return false;
  exe_path = exe_path.Append(installer_util::kInstallerDir);
  exe_path = exe_path.Append(installer_util::kSetupExe);
  base::ProcessHandle ph;
  CommandLine cl(exe_path);
  cl.AppendSwitchWithValue(param, value);

  CommandLine* browser_command_line = CommandLine::ForCurrentProcess();
  if (browser_command_line->HasSwitch(switches::kChromeFrame)) {
    cl.AppendSwitch(switches::kChromeFrame);
  }

  if (!base::LaunchApp(cl, false, false, &ph))
    return false;
  DWORD wr = ::WaitForSingleObject(ph, INFINITE);
  if (wr != WAIT_OBJECT_0)
    return false;
  return (TRUE == ::GetExitCodeProcess(ph, reinterpret_cast<DWORD*>(ret_code)));
}

bool WriteEULAtoTempFile(FilePath* eula_path) {
  base::StringPiece terms =
      ResourceBundle::GetSharedInstance().GetRawDataResource(IDR_TERMS_HTML);
  if (terms.empty())
    return false;
  FilePath temp_dir;
  if (!file_util::GetTempDir(&temp_dir))
    return false;
  *eula_path = temp_dir.Append(L"chrome_eula_iframe.html");
  return (file_util::WriteFile(*eula_path, terms.data(), terms.size()) > 0);
}

// Helper class that performs delayed first-run tasks that need more of the
// chrome infrastructure to be up an running before they can be attempted.
class FirsRunDelayedTasks : public NotificationObserver {
 public:
  enum Tasks {
    NO_TASK,
    INSTALL_EXTENSIONS
  };

  explicit FirsRunDelayedTasks(Tasks task) {
    if (task == INSTALL_EXTENSIONS) {
      registrar_.Add(this, NotificationType::EXTENSIONS_READY,
                     NotificationService::AllSources());
    }
    registrar_.Add(this, NotificationType::BROWSER_CLOSED,
                   NotificationService::AllSources());
  }

  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details) {
    // After processing the notification we always delete ourselves.
    if (type.value == NotificationType::EXTENSIONS_READY)
      DoExtensionWork(Source<Profile>(source).ptr()->GetExtensionsService());
    delete this;
    return;
  }

 private:
  // Private ctor forces it to be created only in the heap.
  ~FirsRunDelayedTasks() {}

  // The extension work is to basically trigger an extension update check.
  // If the extension specified in the master pref is older than the live
  // extension it will get updated which is the same as get it installed.
  void DoExtensionWork(ExtensionsService* service) {
    if (!service)
      return;
    service->updater()->CheckNow();
    return;
  }

  NotificationRegistrar registrar_;
};

}  // namespace

CommandLine* Upgrade::new_command_line_ = NULL;

bool FirstRun::CreateChromeDesktopShortcut() {
  std::wstring chrome_exe;
  if (!PathService::Get(base::FILE_EXE, &chrome_exe))
    return false;
  BrowserDistribution *dist = BrowserDistribution::GetDistribution();
  if (!dist)
    return false;
  return ShellUtil::CreateChromeDesktopShortcut(chrome_exe,
      dist->GetAppDescription(), ShellUtil::CURRENT_USER,
      false, true);  // create if doesn't exist.
}

bool FirstRun::CreateChromeQuickLaunchShortcut() {
  std::wstring chrome_exe;
  if (!PathService::Get(base::FILE_EXE, &chrome_exe))
    return false;
  return ShellUtil::CreateChromeQuickLaunchShortcut(chrome_exe,
      ShellUtil::CURRENT_USER,  // create only for current user.
      true);  // create if doesn't exist.
}

bool FirstRun::ProcessMasterPreferences(const FilePath& user_data_dir,
                                        MasterPrefs* out_prefs) {
  DCHECK(!user_data_dir.empty());

  // The standard location of the master prefs is next to the chrome exe.
  FilePath master_prefs;
  if (!PathService::Get(base::DIR_EXE, &master_prefs))
    return true;
  master_prefs = master_prefs.AppendASCII(installer_util::kDefaultMasterPrefs);

  scoped_ptr<DictionaryValue> prefs(
      installer_util::ParseDistributionPreferences(master_prefs));
  if (!prefs.get())
    return true;

  out_prefs->new_tabs = installer_util::GetFirstRunTabs(prefs.get());

  if (!installer_util::GetDistroIntegerPreference(prefs.get(),
      installer_util::master_preferences::kDistroPingDelay,
      &out_prefs->ping_delay)) {
    // 90 seconds is the default that we want to use in case master
    // preferences is missing, corrupt or ping_delay is missing.
    out_prefs->ping_delay = 90;
  }

  std::string not_used;
  out_prefs->homepage_defined = prefs->GetString(prefs::kHomePage, &not_used);

  bool value = false;
  if (installer_util::GetDistroBooleanPreference(prefs.get(),
      installer_util::master_preferences::kRequireEula, &value) && value) {
    // Show the post-installation EULA. This is done by setup.exe and the
    // result determines if we continue or not. We wait here until the user
    // dismisses the dialog.

    // The actual eula text is in a resource in chrome. We extract it to
    // a text file so setup.exe can use it as an inner frame.
    FilePath inner_html;
    if (WriteEULAtoTempFile(&inner_html)) {
      int retcode = 0;
      const std::string eula = WideToASCII(installer_util::switches::kShowEula);
      if (!LaunchSetupWithParam(eula, inner_html.ToWStringHack(), &retcode) ||
          (retcode == installer_util::EULA_REJECTED)) {
        LOG(WARNING) << "EULA rejected. Fast exit.";
        ::ExitProcess(1);
      }
      if (retcode == installer_util::EULA_ACCEPTED) {
        LOG(INFO) << "EULA : no collection";
        GoogleUpdateSettings::SetCollectStatsConsent(false);
      } else if (retcode == installer_util::EULA_ACCEPTED_OPT_IN) {
        LOG(INFO) << "EULA : collection consent";
        GoogleUpdateSettings::SetCollectStatsConsent(true);
      }
    }
  }

  if (installer_util::GetDistroBooleanPreference(prefs.get(),
      installer_util::master_preferences::kAltFirstRunBubble, &value) && value)
    FirstRun::SetOEMFirstRunBubblePref();

  FilePath user_prefs = GetDefaultPrefFilePath(true, user_data_dir);
  if (user_prefs.empty())
    return true;

  // The master prefs are regular prefs so we can just copy the file
  // to the default place and they just work.
  if (!file_util::CopyFile(master_prefs, user_prefs))
    return true;

  DictionaryValue* extensions = 0;
  if (installer_util::HasExtensionsBlock(prefs.get(), &extensions)) {
    LOG(INFO) << "Extensions block found in master preferences";
    new FirsRunDelayedTasks(FirsRunDelayedTasks::INSTALL_EXTENSIONS);
  }

  // Add a special exception for import_search_engine preference.
  // Even though we skip all other import_* preferences below, if
  // skip-first-run-ui is not specified, we make exception for this one
  // preference.
  int import_items = 0;
  if (installer_util::GetDistroBooleanPreference(prefs.get(),
      installer_util::master_preferences::kDistroImportSearchPref, &value)) {
    if (value) {
      import_items += importer::SEARCH_ENGINES;
      out_prefs->do_import_items += importer::SEARCH_ENGINES;
    } else {
      out_prefs->dont_import_items += importer::SEARCH_ENGINES;
    }
  }

  // If we're suppressing the first-run bubble, set that preference now.
  // Otherwise, wait until the user has completed first run to set it, so the
  // user is guaranteed to see the bubble iff he or she has completed the first
  // run process.
  if (installer_util::GetDistroBooleanPreference(prefs.get(),
      installer_util::master_preferences::kDistroSuppressFirstRunBubble,
      &value) && value)
    FirstRun::SetShowFirstRunBubblePref(false);

  if (InSearchExperimentLocale() &&
      installer_util::GetDistroBooleanPreference(prefs.get(),
      installer_util::master_preferences::kSearchEngineExperimentPref,
      &value) && value) {
    // Set the first run dialog to include the search choice window.
    out_prefs->run_search_engine_experiment = true;
    // Check to see if search engine logos should be randomized.
    if (installer_util::GetDistroBooleanPreference(prefs.get(),
        installer_util::master_preferences::
            kSearchEngineExperimentRandomizePref,
        &value) && value) {
      out_prefs->randomize_search_engine_experiment = true;
    }
    // Set the first run bubble to minimal.
    FirstRun::SetMinimalFirstRunBubblePref();
  }

  // History is imported automatically, unless turned off in master_prefs.
  if (installer_util::GetDistroBooleanPreference(prefs.get(),
      installer_util::master_preferences::kDistroImportHistoryPref, &value)
      && !value) {
    out_prefs->dont_import_items |= importer::HISTORY;
  }

  // Home page is imported automatically only in organic builds, and can be
  // turned off in master_prefs.
  if (installer_util::GetDistroBooleanPreference(prefs.get(),
      installer_util::master_preferences::kDistroImportHomePagePref, &value)
      && !value) {
    out_prefs->dont_import_items |= importer::HOME_PAGE;
  }

  // Bookmarks are never imported unless specifically turned on.
  if (installer_util::GetDistroBooleanPreference(prefs.get(),
      installer_util::master_preferences::kDistroImportBookmarksPref, &value)
      && value) {
    out_prefs->do_import_items |= importer::FAVORITES;
  }

  // Note we are skipping all other master preferences if skip-first-run-ui
  // is *not* specified. (That is, we continue only if skipping first run ui.)
  if (!installer_util::GetDistroBooleanPreference(prefs.get(),
      installer_util::master_preferences::kDistroSkipFirstRunPref, &value) ||
      !value)
    return true;

  // From here on we won't show first run so we need to do the work to show the
  // bubble anyway, unless it's already been explicitly suppressed.
  FirstRun::SetShowFirstRunBubblePref(true);

  // We need to be able to create the first run sentinel or else we cannot
  // proceed because ImportSettings will launch the importer process which
  // would end up here if the sentinel is not present.
  if (!FirstRun::CreateSentinel())
    return false;

  if (installer_util::GetDistroBooleanPreference(prefs.get(),
      installer_util::master_preferences::kDistroShowWelcomePage, &value) &&
      value)
    FirstRun::SetShowWelcomePagePref();

  std::wstring import_bookmarks_path;
  installer_util::GetDistroStringPreference(prefs.get(),
      installer_util::master_preferences::kDistroImportBookmarksFromFilePref,
      &import_bookmarks_path);

  if (import_items || !import_bookmarks_path.empty()) {
    // There is something to import from the default browser. This launches
    // the importer process and blocks until done or until it fails.
    scoped_refptr<ImporterHost> importer_host = new ImporterHost();
    if (!FirstRun::ImportSettings(NULL,
          importer_host->GetSourceProfileInfoAt(0).browser_type,
          import_items, import_bookmarks_path, NULL)) {
      LOG(WARNING) << "silent import failed";
    }
  }

  if (installer_util::GetDistroBooleanPreference(prefs.get(),
      installer_util::master_preferences::kMakeChromeDefaultForUser, &value) &&
      value)
    ShellIntegration::SetAsDefaultBrowser();

  return false;
}

bool Upgrade::IsBrowserAlreadyRunning() {
  static HANDLE handle = NULL;
  std::wstring exe;
  PathService::Get(base::FILE_EXE, &exe);
  std::replace(exe.begin(), exe.end(), '\\', '!');
  std::transform(exe.begin(), exe.end(), exe.begin(), tolower);
  exe = L"Global\\" + exe;
  if (handle != NULL)
    CloseHandle(handle);
  handle = CreateEvent(NULL, TRUE, TRUE, exe.c_str());
  int error = GetLastError();
  return (error == ERROR_ALREADY_EXISTS || error == ERROR_ACCESS_DENIED);
}

bool Upgrade::RelaunchChromeBrowser(const CommandLine& command_line) {
  ::SetEnvironmentVariable(
    BrowserDistribution::GetDistribution()->GetEnvVersionKey().c_str(),
    NULL);
  return base::LaunchApp(command_line.command_line_string(),
                         false, false, NULL);
}

bool Upgrade::SwapNewChromeExeIfPresent() {
  FilePath new_chrome_exe;
  if (!GetNewerChromeFile(&new_chrome_exe))
    return false;
  if (!file_util::PathExists(new_chrome_exe))
    return false;
  std::wstring curr_chrome_exe;
  if (!PathService::Get(base::FILE_EXE, &curr_chrome_exe))
    return false;

  // First try to rename exe by launching rename command ourselves.
  bool user_install = InstallUtil::IsPerUserInstall(curr_chrome_exe.c_str());
  HKEY reg_root = user_install ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
  BrowserDistribution *dist = BrowserDistribution::GetDistribution();
  RegKey key;
  std::wstring rename_cmd;
  if (key.Open(reg_root, dist->GetVersionKey().c_str(), KEY_READ) &&
      key.ReadValue(google_update::kRegRenameCmdField, &rename_cmd)) {
    base::ProcessHandle handle;
    if (base::LaunchApp(rename_cmd, true, true, &handle)) {
      DWORD exit_code;
      ::GetExitCodeProcess(handle, &exit_code);
      ::CloseHandle(handle);
      if (exit_code == installer_util::RENAME_SUCCESSFUL)
        return true;
    }
  }

  // Rename didn't work so try to rename by calling Google Update
  if (InvokeGoogleUpdateForRename())
    return true;

  // Rename still didn't work so just try to rename exe ourselves (for
  // backward compatibility, can be deleted once the new process works).
  std::wstring backup_exe;
  if (!GetBackupChromeFile(&backup_exe))
    return false;
  if (::ReplaceFileW(curr_chrome_exe.c_str(), new_chrome_exe.value().c_str(),
                     backup_exe.c_str(), REPLACEFILE_IGNORE_MERGE_ERRORS,
                     NULL, NULL)) {
    return true;
  }
  return false;
}

// static
bool Upgrade::DoUpgradeTasks(const CommandLine& command_line) {
  if (!Upgrade::SwapNewChromeExeIfPresent())
    return false;
  // At this point the chrome.exe has been swapped with the new one.
  if (!Upgrade::RelaunchChromeBrowser(command_line)) {
    // The re-launch fails. Feel free to panic now.
    NOTREACHED();
  }
  return true;
}

// static
bool Upgrade::IsUpdatePendingRestart() {
  FilePath new_chrome_exe;
  if (!GetNewerChromeFile(&new_chrome_exe))
    return false;
  return file_util::PathExists(new_chrome_exe);
}

namespace {

// This class is used by FirstRun::ImportSettings to determine when the import
// process has ended and what was the result of the operation as reported by
// the process exit code. This class executes in the context of the main chrome
// process.
class ImportProcessRunner : public base::ObjectWatcher::Delegate {
 public:
  // The constructor takes the importer process to watch and then it does a
  // message loop blocking wait until the process ends. This object now owns
  // the import_process handle.
  explicit ImportProcessRunner(base::ProcessHandle import_process)
      : import_process_(import_process),
        exit_code_(ResultCodes::NORMAL_EXIT) {
    watcher_.StartWatching(import_process, this);
    MessageLoop::current()->Run();
  }
  virtual ~ImportProcessRunner() {
    ::CloseHandle(import_process_);
  }
  // Returns the child process exit code. There are 3 expected values:
  // NORMAL_EXIT, IMPORTER_CANCEL or IMPORTER_HUNG.
  int exit_code() const {
    return exit_code_;
  }
  // The child process has terminated. Find the exit code and quit the loop.
  virtual void OnObjectSignaled(HANDLE object) {
    DCHECK(object == import_process_);
    if (!::GetExitCodeProcess(import_process_, &exit_code_)) {
      NOTREACHED();
    }
    MessageLoop::current()->Quit();
  }

 private:
  base::ObjectWatcher watcher_;
  base::ProcessHandle import_process_;
  DWORD exit_code_;
};

// Check every 3 seconds if the importer UI has hung.
const int kPollHangFrequency = 3000;

// This class specializes on finding hung 'owned' windows. Unfortunately, the
// HungWindowDetector class cannot be used here because it assumes child
// windows and not owned top-level windows.
// This code is executed in the context of the main browser process and will
// terminate the importer process if it is hung.
class HungImporterMonitor : public WorkerThreadTicker::Callback {
 public:
  // The ctor takes the owner popup window and the process handle of the
  // process to kill in case the popup or its owned active popup become
  // unresponsive.
  HungImporterMonitor(HWND owner_window, base::ProcessHandle import_process)
      : owner_window_(owner_window),
        import_process_(import_process),
        ticker_(kPollHangFrequency) {
    ticker_.RegisterTickHandler(this);
    ticker_.Start();
  }
  virtual ~HungImporterMonitor() {
    ticker_.Stop();
    ticker_.UnregisterTickHandler(this);
  }

 private:
  virtual void OnTick() {
    if (!import_process_)
      return;
    // We find the top active popup that we own, this will be either the
    // owner_window_ itself or the dialog window of the other process. In
    // both cases it is worth hung testing because both windows share the
    // same message queue and at some point the other window could be gone
    // while the other process still not pumping messages.
    HWND active_window = ::GetLastActivePopup(owner_window_);
    if (::IsHungAppWindow(active_window) || ::IsHungAppWindow(owner_window_)) {
      ::TerminateProcess(import_process_, ResultCodes::IMPORTER_HUNG);
      import_process_ = NULL;
    }
  }

  HWND owner_window_;
  base::ProcessHandle import_process_;
  WorkerThreadTicker ticker_;
  DISALLOW_COPY_AND_ASSIGN(HungImporterMonitor);
};

std::wstring EncodeImportParams(int browser_type, int options, HWND window) {
  return StringPrintf(L"%d@%d@%d", browser_type, options, window);
}

bool DecodeImportParams(const std::wstring& encoded,
                        int* browser_type, int* options, HWND* window) {
  std::vector<std::wstring> v;
  SplitString(encoded, L'@', &v);
  if (v.size() != 3)
    return false;

  if (!StringToInt(v[0], browser_type))
    return false;

  if (!StringToInt(v[1], options))
    return false;

  *window = reinterpret_cast<HWND>(StringToInt64(v[2]));
  return true;
}

}  // namespace

void FirstRun::AutoImport(Profile* profile,
    bool homepage_defined,
    int import_items,
    int dont_import_items,
    bool search_engine_experiment,
    bool randomize_search_engine_experiment,
    ProcessSingleton* process_singleton) {
  FirstRun::CreateChromeDesktopShortcut();
  // Windows 7 has deprecated the quick launch bar.
  if (win_util::GetWinVersion() < win_util::WINVERSION_WIN7)
    CreateChromeQuickLaunchShortcut();

  scoped_refptr<ImporterHost> importer_host;
  importer_host = new ImporterHost();
  int items = 0;
  // History and home page are always imported unless turned off in
  // master_preferences.
  if (!(dont_import_items & importer::HISTORY))
    items = items | importer::HISTORY;
  if (!((dont_import_items & importer::HOME_PAGE) || homepage_defined))
    items = items | importer::HOME_PAGE;

  // Search engine and bookmarks are never imported unless turned on
  // in master_preferences.
  if (import_items & importer::SEARCH_ENGINES)
    items = items | importer::SEARCH_ENGINES;
  if (import_items & importer::FAVORITES)
    items = items | importer::FAVORITES;
  // We need to avoid dispatching new tabs when we are importing because
  // that will lead to data corruption or a crash. Because there is no UI for
  // the import process, we pass NULL as the window to bring to the foreground
  // when a CopyData message comes in; this causes the message to be silently
  // discarded, which is the correct behavior during the import process.
  process_singleton->Lock(NULL);

  // Index 0 is the default browser.
  FirstRun::ImportSettings(profile,
      importer_host->GetSourceProfileInfoAt(0).browser_type, items, NULL);
  UserMetrics::RecordAction(UserMetricsAction("FirstRunDef_Accept"));

  // Launch the search engine dialog only if build is organic.
  std::wstring brand;
  GoogleUpdateSettings::GetBrand(&brand);
  if (GoogleUpdateSettings::IsOrganic(brand)) {
    // The home page string may be set in the preferences, but the user should
    // initially use Chrome with the NTP as home page in organic builds.
    profile->GetPrefs()->SetBoolean(prefs::kHomePageIsNewTabPage, true);

    // Search engine dialog is shown in organic builds unless overridden by
    // master_preferences.
    if (!(import_items & importer::SEARCH_ENGINES)) {
      views::Window* search_engine_dialog = views::Window::CreateChromeWindow(
          NULL,
          gfx::Rect(),
          new FirstRunSearchEngineView(profile,
          randomize_search_engine_experiment));
      DCHECK(search_engine_dialog);

      search_engine_dialog->Show();
      views::AcceleratorHandler accelerator_handler;
      MessageLoopForUI::current()->Run(&accelerator_handler);
      search_engine_dialog->Close();
    }
  }

  process_singleton->Unlock();
  FirstRun::CreateSentinel();
}

bool FirstRun::ImportSettings(Profile* profile, int browser_type,
                              int items_to_import,
                              const std::wstring& import_bookmarks_path,
                              HWND parent_window) {
  const CommandLine& cmdline = *CommandLine::ForCurrentProcess();
  CommandLine import_cmd(cmdline.GetProgram());
  // Propagate user data directory switch.
  if (cmdline.HasSwitch(switches::kUserDataDir)) {
    import_cmd.AppendSwitchWithValue(
        switches::kUserDataDir,
        cmdline.GetSwitchValueASCII(switches::kUserDataDir));
  }

  // Since ImportSettings is called before the local state is stored on disk
  // we pass the language as an argument.  GetApplicationLocale checks the
  // current command line as fallback.
  import_cmd.AppendSwitchWithValue(
      switches::kLang,
      ASCIIToWide(g_browser_process->GetApplicationLocale()));

  if (items_to_import) {
    import_cmd.CommandLine::AppendSwitchWithValue(switches::kImport,
        EncodeImportParams(browser_type, items_to_import, parent_window));
  }

  if (!import_bookmarks_path.empty()) {
    import_cmd.CommandLine::AppendSwitchWithValue(
        switches::kImportFromFile, import_bookmarks_path.c_str());
  }

  if (cmdline.HasSwitch(switches::kChromeFrame)) {
    import_cmd.AppendSwitch(switches::kChromeFrame);
  }

  if (cmdline.HasSwitch(switches::kCountry)) {
    import_cmd.AppendSwitchWithValue(switches::kCountry,
      cmdline.GetSwitchValueASCII(switches::kCountry));
  }

  // Time to launch the process that is going to do the import.
  base::ProcessHandle import_process;
  if (!base::LaunchApp(import_cmd, false, false, &import_process))
    return false;

  // Activate the importer monitor. It awakes periodically in another thread
  // and checks that the importer UI is still pumping messages.
  if (parent_window)
    HungImporterMonitor hang_monitor(parent_window, import_process);

  // We block inside the import_runner ctor, pumping messages until the
  // importer process ends. This can happen either by completing the import
  // or by hang_monitor killing it.
  ImportProcessRunner import_runner(import_process);

  // Import process finished. Reload the prefs, because importer may set
  // the pref value.
  if (profile)
    profile->GetPrefs()->ReloadPersistentPrefs();

  return (import_runner.exit_code() == ResultCodes::NORMAL_EXIT);
}

bool FirstRun::ImportSettings(Profile* profile, int browser_type,
                              int items_to_import,
                              HWND parent_window) {
  return ImportSettings(profile, browser_type, items_to_import,
                        std::wstring(), parent_window);
}

int FirstRun::ImportFromBrowser(Profile* profile,
                                const CommandLine& cmdline) {
  std::wstring import_info = cmdline.GetSwitchValue(switches::kImport);
  if (import_info.empty()) {
    NOTREACHED();
    return false;
  }
  int browser_type = 0;
  int items_to_import = 0;
  HWND parent_window = NULL;
  if (!DecodeImportParams(import_info, &browser_type, &items_to_import,
                          &parent_window)) {
    NOTREACHED();
    return false;
  }
  scoped_refptr<ImporterHost> importer_host = new ImporterHost();
  FirstRunImportObserver observer;

  // If there is no parent window, we run in headless mode which amounts
  // to having the windows hidden and if there is user action required the
  // import is automatically canceled.
  if (!parent_window)
    importer_host->set_headless();

  StartImportingWithUI(
      parent_window,
      items_to_import,
      importer_host,
      importer_host->GetSourceProfileInfoForBrowserType(browser_type),
      profile,
      &observer,
      true);
  observer.RunLoop();
  return observer.import_result();
}

// static
bool FirstRun::InSearchExperimentLocale() {
  static std::set<std::string> allowed_locales;
  if (allowed_locales.empty()) {
    // List of locales in which search experiment can be run.
    allowed_locales.insert("en-GB");
    allowed_locales.insert("en-US");
  }
  const std::string app_locale = g_browser_process->GetApplicationLocale();
  std::set<std::string>::iterator locale = allowed_locales.find(app_locale);
  return locale != allowed_locales.end();
}

//////////////////////////////////////////////////////////////////////////

namespace {

const wchar_t kHelpCenterUrl[] =
    L"http://www.google.com/support/chrome/bin/answer.py?answer=150752";

// This class displays a modal dialog using the views system. The dialog asks
// the user to give chrome another try. This class only handles the UI so the
// resulting actions are up to the caller. One version looks like this:
//
//   /----------------------------------------\
//   | |icon| You stopped using Google    [x] |
//   | |icon| Chrome. Would you like to..     |
//   |        [o] Give the new version a try  |
//   |        [ ] Uninstall Google Chrome     |
//   |        [ OK ] [Don't bug me]           |
//   |        _why_am_I_seeign this?__        |
//   ------------------------------------------
class TryChromeDialog : public views::ButtonListener,
                        public views::LinkController {
 public:
  TryChromeDialog()
      : popup_(NULL),
        try_chrome_(NULL),
        kill_chrome_(NULL),
        result_(Upgrade::TD_LAST_ENUM) {
  }

  virtual ~TryChromeDialog() {
  };

  // Shows the modal dialog asking the user to try chrome. Note that the dialog
  // has no parent and it will position itself in a lower corner of the screen.
  // The dialog does not steal focus and does not have an entry in the taskbar.
  Upgrade::TryResult ShowModal() {
    using views::GridLayout;
    ResourceBundle& rb = ResourceBundle::GetSharedInstance();

    views::ImageView* icon = new views::ImageView();
    icon->SetImage(*rb.GetBitmapNamed(IDR_PRODUCT_ICON_32));
    gfx::Size icon_size = icon->GetPreferredSize();

    // An approximate window size. After Layout() we'll get better bounds.
    gfx::Rect pos(310, 160);
    views::WidgetWin* popup = new views::WidgetWin();
    if (!popup) {
      NOTREACHED();
      return Upgrade::TD_DIALOG_ERROR;
    }
    popup->set_delete_on_destroy(true);
    popup->set_window_style(WS_POPUP | WS_CLIPCHILDREN);
    popup->set_window_ex_style(WS_EX_TOOLWINDOW);
    popup->Init(NULL, pos);

    views::RootView* root_view = popup->GetRootView();
    // The window color is a tiny bit off-white.
    root_view->set_background(
        views::Background::CreateSolidBackground(0xfc, 0xfc, 0xfc));

    views::GridLayout* layout = CreatePanelGridLayout(root_view);
    if (!layout) {
      NOTREACHED();
      return Upgrade::TD_DIALOG_ERROR;
    }
    root_view->SetLayoutManager(layout);

    views::ColumnSet* columns;
    // First row: [icon][pad][text][button].
    columns = layout->AddColumnSet(0);
    columns->AddColumn(GridLayout::LEADING, GridLayout::LEADING, 0,
                       GridLayout::FIXED, icon_size.width(),
                       icon_size.height());
    columns->AddPaddingColumn(0, kRelatedControlHorizontalSpacing);
    columns->AddColumn(GridLayout::FILL, GridLayout::FILL, 1,
                       GridLayout::USE_PREF, 0, 0);
    columns->AddColumn(GridLayout::TRAILING, GridLayout::FILL, 1,
                       GridLayout::USE_PREF, 0, 0);
    // Second row: [pad][pad][radio 1].
    columns = layout->AddColumnSet(1);
    columns->AddPaddingColumn(0, icon_size.width());
    columns->AddPaddingColumn(0, kRelatedControlHorizontalSpacing);
    columns->AddColumn(GridLayout::LEADING, GridLayout::FILL, 1,
                       GridLayout::USE_PREF, 0, 0);
    // Third row: [pad][pad][radio 2].
    columns = layout->AddColumnSet(2);
    columns->AddPaddingColumn(0, icon_size.width());
    columns->AddPaddingColumn(0, kRelatedControlHorizontalSpacing);
    columns->AddColumn(GridLayout::LEADING, GridLayout::FILL, 1,
                       GridLayout::USE_PREF, 0, 0);
    // Fourth row: [pad][pad][button][pad][button].
    columns = layout->AddColumnSet(3);
    columns->AddPaddingColumn(0, icon_size.width());
    columns->AddPaddingColumn(0, kRelatedControlHorizontalSpacing);
    columns->AddColumn(GridLayout::LEADING, GridLayout::FILL, 0,
                       GridLayout::USE_PREF, 0, 0);
    columns->AddPaddingColumn(0, kRelatedButtonHSpacing);
    columns->AddColumn(GridLayout::LEADING, GridLayout::FILL, 0,
                       GridLayout::USE_PREF, 0, 0);
    // Fifth row: [pad][pad][link].
    columns = layout->AddColumnSet(4);
    columns->AddPaddingColumn(0, icon_size.width());
    columns->AddPaddingColumn(0, kRelatedControlHorizontalSpacing);
    columns->AddColumn(GridLayout::LEADING, GridLayout::FILL, 1,
                       GridLayout::USE_PREF, 0, 0);
    // First row views.
    layout->StartRow(0, 0);
    layout->AddView(icon);
    // The heading has two flavors of text, the alt one features extensions but
    // we only use it in the US until some international issues are fixed.
    const std::string app_locale = g_browser_process->GetApplicationLocale();
    const std::wstring heading = (app_locale == "en-US") ?
        l10n_util::GetString(IDS_TRY_TOAST_ALT_HEADING) :
        l10n_util::GetString(IDS_TRY_TOAST_HEADING);
    views::Label* label =
        new views::Label(heading);
    label->SetFont(rb.GetFont(ResourceBundle::MediumBoldFont));
    label->SetMultiLine(true);
    label->SizeToFit(200);
    label->SetHorizontalAlignment(views::Label::ALIGN_LEFT);
    layout->AddView(label);
    // The close button is custom.
    views::ImageButton* close_button = new views::ImageButton(this);
    close_button->SetImage(views::CustomButton::BS_NORMAL,
                          rb.GetBitmapNamed(IDR_CLOSE_BAR));
    close_button->SetImage(views::CustomButton::BS_HOT,
                          rb.GetBitmapNamed(IDR_CLOSE_BAR_H));
    close_button->SetImage(views::CustomButton::BS_PUSHED,
                          rb.GetBitmapNamed(IDR_CLOSE_BAR_P));
    close_button->set_tag(BT_CLOSE_BUTTON);
    layout->AddView(close_button);

    // Second row views.
    const std::wstring try_it(l10n_util::GetString(IDS_TRY_TOAST_TRY_OPT));
    layout->StartRowWithPadding(0, 1, 0, 10);
    try_chrome_ = new views::RadioButton(try_it, 1);
    layout->AddView(try_chrome_);
    try_chrome_->SetChecked(true);

    // Third row views.
    const std::wstring
        kill_it(l10n_util::GetString(IDS_UNINSTALL_CHROME));
    layout->StartRow(0, 2);
    kill_chrome_ = new views::RadioButton(kill_it, 1);
    layout->AddView(kill_chrome_);

    // Fourth row views.
    const std::wstring ok_it(l10n_util::GetString(IDS_OK));
    const std::wstring cancel_it(l10n_util::GetString(IDS_TRY_TOAST_CANCEL));
    const std::wstring why_this(l10n_util::GetString(IDS_TRY_TOAST_WHY));
    layout->StartRowWithPadding(0, 3, 0, 10);
    views::Button* accept_button = new views::NativeButton(this, ok_it);
    accept_button->set_tag(BT_OK_BUTTON);
    layout->AddView(accept_button);
    views::Button* cancel_button = new views::NativeButton(this, cancel_it);
    cancel_button->set_tag(BT_CLOSE_BUTTON);
    layout->AddView(cancel_button);
    // Fifth row views.
    layout->StartRowWithPadding(0, 4, 0, 10);
    views::Link* link = new views::Link(why_this);
    link->SetController(this);
    layout->AddView(link);

    // We resize the window according to the layout manager. This takes into
    // account the differences between XP and Vista fonts and buttons.
    layout->Layout(root_view);
    gfx::Size preferred = layout->GetPreferredSize(root_view);
    pos = ComputeWindowPosition(preferred.width(), preferred.height(),
                                base::i18n::IsRTL());
    popup->SetBounds(pos);

    // Carve the toast shape into the window.
    SetToastRegion(popup->GetNativeView(),
                   preferred.width(), preferred.height());
    // Time to show the window in a modal loop.
    popup_ = popup;
    popup_->Show();
    MessageLoop::current()->Run();
    return result_;
  }

 protected:
  // Overridden from ButtonListener. We have two buttons and according to
  // what the user clicked we set |result_| and we should always close and
  // end the modal loop.
  virtual void ButtonPressed(views::Button* sender, const views::Event& event) {
    if (sender->tag() == BT_CLOSE_BUTTON) {
      // The user pressed cancel or the [x] button.
      result_ = Upgrade::TD_NOT_NOW;
    } else if (!try_chrome_) {
      // We don't have radio buttons, the user pressed ok.
      result_ = Upgrade::TD_TRY_CHROME;
    } else {
      // The outcome is according to the selected ratio button.
      result_ = try_chrome_->checked() ? Upgrade::TD_TRY_CHROME :
                                         Upgrade::TD_UNINSTALL_CHROME;
    }
    popup_->Close();
    MessageLoop::current()->Quit();
  }

  // Overridden from LinkController. If the user selects the link we need to
  // fire off the default browser that by some convoluted logic should not be
  // chrome.
  virtual void LinkActivated(views::Link* source, int event_flags) {
    ::ShellExecuteW(NULL, L"open", kHelpCenterUrl, NULL, NULL, SW_SHOW);
  }

 private:
  enum ButtonTags {
    BT_NONE,
    BT_CLOSE_BUTTON,
    BT_OK_BUTTON,
  };

  // Returns a screen rectangle that is fit to show the window. In particular
  // it has the following properties: a) is visible and b) is attached to
  // the bottom of the working area. For LTR machines it returns a left side
  // rectangle and for RTL it returns a right side rectangle so that the
  // dialog does not compete with the standar place of the start menu.
  gfx::Rect ComputeWindowPosition(int width, int height, bool is_RTL) {
    // The 'Shell_TrayWnd' is the taskbar. We like to show our window in that
    // monitor if we can. This code works even if such window is not found.
    HWND taskbar = ::FindWindowW(L"Shell_TrayWnd", NULL);
    HMONITOR monitor =
        ::MonitorFromWindow(taskbar, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info = {sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) {
      // Quite unexpected. Do a best guess at a visible rectangle.
      return gfx::Rect(20, 20, width + 20, height + 20);
    }
    // The |rcWork| is the work area. It should account for the taskbars that
    // are in the screen when we called the function.
    int left = is_RTL ? info.rcWork.left : info.rcWork.right - width;
    int top = info.rcWork.bottom - height;
    return gfx::Rect(left, top, width, height);
  }

  // Create a windows region that looks like a toast of width |w| and
  // height |h|. This is best effort, so we don't care much if the operation
  // fails.
  void SetToastRegion(HWND window, int w, int h) {
    static const POINT polygon[] = {
      {0,   4}, {1,   2}, {2,   1}, {4, 0},   // Left side.
      {w-4, 0}, {w-2, 1}, {w-1, 2}, {w, 4},   // Right side.
      {w, h}, {0, h}
    };
    HRGN region = ::CreatePolygonRgn(polygon, arraysize(polygon), WINDING);
    ::SetWindowRgn(window, region, FALSE);
  }

  // controls which version of the text to use.
  size_t version_;

  // We don't own any of this pointers. The |popup_| owns itself and owns
  // the other views.
  views::WidgetWin* popup_;
  views::RadioButton* try_chrome_;
  views::RadioButton* kill_chrome_;
  Upgrade::TryResult result_;

  DISALLOW_COPY_AND_ASSIGN(TryChromeDialog);
};

}  // namespace

Upgrade::TryResult Upgrade::ShowTryChromeDialog(size_t version) {
  if (version > 10000) {
    // This is a test value. We want to make sure we exercise
    // returning this early. See EarlyReturnTest test harness.
    return Upgrade::TD_NOT_NOW;
  }
  TryChromeDialog td;
  return td.ShowModal();
}
