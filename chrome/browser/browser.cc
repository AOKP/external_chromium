// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/browser.h"

#if defined(OS_WIN)
#include <shellapi.h>
#include <windows.h>
#endif  // OS_WIN

#include <algorithm>
#include <string>

#include "app/animation.h"
#include "app/l10n_util.h"
#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/keyboard_codes.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/string_util.h"
#include "base/thread.h"
#include "gfx/point.h"
#include "chrome/app/chrome_dll_resource.h"
#include "chrome/browser/autofill/autofill_manager.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/bookmarks/bookmark_utils.h"
#include "chrome/browser/browser_list.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_shutdown.h"
#include "chrome/browser/browser_window.h"
#include "chrome/browser/browser_url_handler.h"
#include "chrome/browser/character_encoding.h"
#include "chrome/browser/debugger/devtools_manager.h"
#include "chrome/browser/debugger/devtools_toggle_action.h"
#include "chrome/browser/debugger/devtools_window.h"
#include "chrome/browser/dock_info.h"
#include "chrome/browser/dom_ui/filebrowse_ui.h"
#include "chrome/browser/download/download_item.h"
#include "chrome/browser/download/download_item_model.h"
#include "chrome/browser/download/download_manager.h"
#include "chrome/browser/download/download_shelf.h"
#include "chrome/browser/download/download_started_animation.h"
#include "chrome/browser/extensions/crashed_extension_infobar.h"
#include "chrome/browser/extensions/extension_browser_event_router.h"
#include "chrome/browser/extensions/extension_disabled_infobar_delegate.h"
#include "chrome/browser/extensions/extension_shelf_model.h"
#include "chrome/browser/extensions/extension_tabs_module.h"
#include "chrome/browser/extensions/extensions_service.h"
#include "chrome/browser/find_bar.h"
#include "chrome/browser/find_bar_controller.h"
#include "chrome/browser/first_run.h"
#include "chrome/browser/google_url_tracker.h"
#include "chrome/browser/google_util.h"
#include "chrome/browser/host_zoom_map.h"
#include "chrome/browser/location_bar.h"
#include "chrome/browser/metrics/user_metrics.h"
#include "chrome/browser/net/browser_url_util.h"
#include "chrome/browser/net/url_fixer_upper.h"
#include "chrome/browser/options_window.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/pref_service.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/renderer_host/render_view_host.h"
#include "chrome/browser/renderer_host/site_instance.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_types.h"
#include "chrome/browser/status_bubble.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/sync/sync_ui_util.h"
#include "chrome/browser/tab_closeable_state_watcher.h"
#include "chrome/browser/tab_contents/interstitial_page.h"
#include "chrome/browser/tab_contents/navigation_controller.h"
#include "chrome/browser/tab_contents/navigation_entry.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/browser/tab_contents/tab_contents_view.h"
#include "chrome/browser/tab_menu_model.h"
#include "chrome/browser/upgrade_detector.h"
#include "chrome/browser/web_applications/web_app.h"
#include "chrome/browser/window_sizer.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/page_transition_types.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "net/base/cookie_monster.h"
#include "net/base/net_util.h"
#include "net/base/registry_controlled_domain.h"
#include "net/base/static_cookie_policy.h"
#include "net/url_request/url_request_context.h"
#include "webkit/glue/window_open_disposition.h"

#if defined(OS_WIN)
#include "app/win_util.h"
#include "chrome/browser/browser_child_process_host.h"
#include "chrome/browser/cert_store.h"
#include "chrome/browser/download/save_package.h"
#include "chrome/browser/ssl/ssl_error_info.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/task_manager.h"
#include "chrome/browser/view_ids.h"
#include "chrome/browser/views/app_launcher.h"
#include "chrome/browser/views/location_bar/location_bar_view.h"
#endif  // OS_WIN

#if defined(OS_MACOSX)
#include "chrome/browser/cocoa/find_pasteboard.h"
#endif

#if defined(OS_CHROMEOS)
#include "chrome/browser/views/app_launcher.h"
#endif

using base::TimeDelta;

// How long we wait before updating the browser chrome while loading a page.
static const int kUIUpdateCoalescingTimeMS = 200;

// The URL to be loaded to display Help.
static const char* const kHelpContentUrl =
    "http://www.google.com/support/chrome/";

// The URL to be loaded to display the "Report a broken page" form.
static const std::string kBrokenPageUrl =
    "http://www.google.com/support/chrome/bin/request.py?contact_type="
    "broken_website&format=inproduct&p.page_title=$1&p.page_url=$2";

///////////////////////////////////////////////////////////////////////////////

namespace {

// Returns true if the specified TabContents has unload listeners registered.
bool TabHasUnloadListener(TabContents* contents) {
  return contents->notify_disconnection() &&
      !contents->showing_interstitial_page() &&
      !contents->render_view_host()->SuddenTerminationAllowed();
}

// Returns true if two URLs are equal ignoring their ref (hash fragment).
bool CompareURLsIgnoreRef(const GURL& url, const GURL& other) {
  if (url == other)
    return true;
  // If neither has a ref than there is no point in stripping the refs and
  // the URLs are different since the comparison failed in the previous if
  // statement.
  if (!url.has_ref() && !other.has_ref())
    return false;
  url_canon::Replacements<char> replacements;
  replacements.ClearRef();
  GURL url_no_ref = url.ReplaceComponents(replacements);
  GURL other_no_ref = other.ReplaceComponents(replacements);
  return url_no_ref == other_no_ref;
}

// Return true if a browser is an app window or panel hosting
// extension |extension_app|, and running under |profile|.
bool BrowserHostsExtensionApp(Browser* browser,
                              Profile* profile,
                              Extension* extension_app) {
  if (browser->profile() != profile)
     return false;

  if (browser->type() != Browser::TYPE_EXTENSION_APP &&
      browser->type() != Browser::TYPE_APP_PANEL)
    return false;

  if (browser->extension_app() != extension_app)
    return false;

  return true;
}

}  // namespace

///////////////////////////////////////////////////////////////////////////////
// Browser, Constructors, Creation, Showing:

Browser::Browser(Type type, Profile* profile)
    : type_(type),
      profile_(profile),
      window_(NULL),
      tabstrip_model_(this, profile),
      command_updater_(this),
      toolbar_model_(this),
      chrome_updater_factory_(this),
      is_attempting_to_close_browser_(false),
      cancel_download_confirmation_state_(NOT_PROMPTED),
      maximized_state_(MAXIMIZED_STATE_DEFAULT),
      method_factory_(this),
      block_command_execution_(false),
      last_blocked_command_id_(-1),
      last_blocked_command_disposition_(CURRENT_TAB),
      pending_web_app_action_(NONE),
      extension_app_(NULL) {
  tabstrip_model_.AddObserver(this);

  registrar_.Add(this, NotificationType::SSL_VISIBLE_STATE_CHANGED,
                 NotificationService::AllSources());
  registrar_.Add(this, NotificationType::EXTENSION_UPDATE_DISABLED,
                 NotificationService::AllSources());
  registrar_.Add(this, NotificationType::EXTENSION_LOADED,
                 NotificationService::AllSources());
  registrar_.Add(this, NotificationType::EXTENSION_UNLOADED,
                 NotificationService::AllSources());
  registrar_.Add(this, NotificationType::EXTENSION_UNLOADED_DISABLED,
                 NotificationService::AllSources());
  registrar_.Add(this, NotificationType::EXTENSION_PROCESS_TERMINATED,
                 NotificationService::AllSources());
  registrar_.Add(this, NotificationType::BROWSER_THEME_CHANGED,
                 NotificationService::AllSources());
  registrar_.Add(this, NotificationType::PROFILE_ERROR,
                 NotificationService::AllSources());

  // Need to know when to alert the user of theme install delay.
  registrar_.Add(this, NotificationType::EXTENSION_READY_FOR_INSTALL,
                 NotificationService::AllSources());

  InitCommandState();
  BrowserList::AddBrowser(this);

  encoding_auto_detect_.Init(prefs::kWebKitUsesUniversalDetector,
                             profile_->GetPrefs(), NULL);
  use_vertical_tabs_.Init(prefs::kUseVerticalTabs, profile_->GetPrefs(), this);
  if (!TabMenuModel::AreVerticalTabsEnabled()) {
    // If vertical tabs aren't enabled, explicitly turn them off. Otherwise we
    // might show vertical tabs but not show an option to turn them off.
    use_vertical_tabs_.SetValue(false);
  }
  UpdateTabStripModelInsertionPolicy();

  tab_restore_service_ = profile->GetTabRestoreService();
  if (tab_restore_service_) {
    tab_restore_service_->AddObserver(this);
    TabRestoreServiceChanged(tab_restore_service_);
  }

  if (profile_->GetProfileSyncService())
    profile_->GetProfileSyncService()->AddObserver(this);
}

Browser::~Browser() {
  // The tab strip should not have any significant tabs at this point.
  DCHECK(!tabstrip_model_.HasNonPhantomTabs());
  tabstrip_model_.RemoveObserver(this);

  if (profile_->GetProfileSyncService())
    profile_->GetProfileSyncService()->RemoveObserver(this);

  BrowserList::RemoveBrowser(this);

#if defined(OS_WIN) || defined(OS_LINUX)
  if (!BrowserList::HasBrowserWithProfile(profile_)) {
    // We're the last browser window with this profile. We need to nuke the
    // TabRestoreService, which will start the shutdown of the
    // NavigationControllers and allow for proper shutdown. If we don't do this
    // chrome won't shutdown cleanly, and may end up crashing when some
    // thread tries to use the IO thread (or another thread) that is no longer
    // valid.
    // This isn't a valid assumption for Mac OS, as it stays running after
    // the last browser has closed. The Mac equivalent is in its app
    // controller.
    profile_->ResetTabRestoreService();
  }
#endif

  SessionService* session_service = profile_->GetSessionService();
  if (session_service)
    session_service->WindowClosed(session_id_);

  TabRestoreService* tab_restore_service = profile()->GetTabRestoreService();
  if (tab_restore_service)
    tab_restore_service->BrowserClosed(this);

  if (profile_->IsOffTheRecord() &&
      !BrowserList::IsOffTheRecordSessionActive()) {
    // An off-the-record profile is no longer needed, this indirectly
    // frees its cache and cookies.
    profile_->GetOriginalProfile()->DestroyOffTheRecordProfile();
  }

  // There may be pending file dialogs, we need to tell them that we've gone
  // away so they don't try and call back to us.
  if (select_file_dialog_.get())
    select_file_dialog_->ListenerDestroyed();

  TabRestoreServiceDestroyed(tab_restore_service_);
}

// static
Browser* Browser::Create(Profile* profile) {
  Browser* browser = new Browser(TYPE_NORMAL, profile);
  browser->CreateBrowserWindow();
  return browser;
}

// static
Browser* Browser::CreateForPopup(Profile* profile) {
  Browser* browser = new Browser(TYPE_POPUP, profile);
  browser->CreateBrowserWindow();
  return browser;
}

// static
Browser* Browser::CreateForApp(const std::wstring& app_name,
                               Extension* extension,
                               Profile* profile,
                               bool is_panel) {
  Browser::Type type = TYPE_APP;

  if (is_panel)
    type = TYPE_APP_PANEL;
  else if (extension)
    type = TYPE_EXTENSION_APP;

  Browser* browser = new Browser(type, profile);
  browser->app_name_ = app_name;
  browser->extension_app_ = extension;

  if (extension) {
    gfx::Rect initial_pos(extension->launch_width(),
                          extension->launch_height());
    if (!initial_pos.IsEmpty())
      browser->set_override_bounds(initial_pos);
  }

  browser->CreateBrowserWindow();

  return browser;
}

// static
Browser* Browser::CreateForDevTools(Profile* profile) {
  Browser* browser = new Browser(TYPE_DEVTOOLS, profile);
  browser->app_name_ = DevToolsWindow::kDevToolsApp;
  browser->CreateBrowserWindow();
  return browser;
}

void Browser::CreateBrowserWindow() {
  DCHECK(!window_);

  if (SupportsWindowFeature(FEATURE_EXTENSIONSHELF))
    extension_shelf_model_.reset(new ExtensionShelfModel(this));

  window_ = BrowserWindow::CreateBrowserWindow(this);

#if defined(OS_WIN)
  // Set the app user model id for this application to that of the application
  // name.  See http://crbug.com/7028.
  win_util::SetAppIdForWindow(
      type_ & TYPE_APP ?
      ShellIntegration::GetAppId(app_name_, profile_->GetPath()) :
      ShellIntegration::GetChromiumAppId(profile_->GetPath()),
      window()->GetNativeHandle());
#endif

  NotificationService::current()->Notify(
      NotificationType::BROWSER_WINDOW_READY,
      Source<Browser>(this),
      NotificationService::NoDetails());

  // Show the First Run information bubble if we've been told to.
  PrefService* local_state = g_browser_process->local_state();
  if (!local_state)
    return;
  if (local_state->FindPreference(prefs::kShouldShowFirstRunBubble) &&
      local_state->GetBoolean(prefs::kShouldShowFirstRunBubble)) {
    FirstRun::BubbleType bubble_type = FirstRun::LARGEBUBBLE;
    if (local_state->
        FindPreference(prefs::kShouldUseOEMFirstRunBubble) &&
        local_state->GetBoolean(prefs::kShouldUseOEMFirstRunBubble)) {
      bubble_type = FirstRun::OEMBUBBLE;
    } else if (local_state->
        FindPreference(prefs::kShouldUseMinimalFirstRunBubble) &&
        local_state->GetBoolean(prefs::kShouldUseMinimalFirstRunBubble)) {
      bubble_type = FirstRun::MINIMALBUBBLE;
    }
    // Reset the preference so we don't show the bubble for subsequent windows.
    local_state->ClearPref(prefs::kShouldShowFirstRunBubble);
    window_->GetLocationBar()->ShowFirstRunBubble(bubble_type);
  }
}

///////////////////////////////////////////////////////////////////////////////
// Getters & Setters

const std::vector<std::wstring>& Browser::user_data_dir_profiles() const {
  return g_browser_process->user_data_dir_profiles();
}

void Browser::set_user_data_dir_profiles(
    const std::vector<std::wstring>& profiles) {
  g_browser_process->user_data_dir_profiles() = profiles;
}

FindBarController* Browser::GetFindBarController() {
  if (!find_bar_controller_.get()) {
    FindBar* find_bar = BrowserWindow::CreateFindBar(this);
    find_bar_controller_.reset(new FindBarController(find_bar));
    find_bar->SetFindBarController(find_bar_controller_.get());
    find_bar_controller_->ChangeTabContents(GetSelectedTabContents());
    find_bar_controller_->find_bar()->MoveWindowIfNecessary(gfx::Rect(), true);
  }
  return find_bar_controller_.get();
}

bool Browser::HasFindBarController() const {
  return find_bar_controller_.get() != NULL;
}

///////////////////////////////////////////////////////////////////////////////
// Browser, Creation Helpers:

// static
void Browser::OpenEmptyWindow(Profile* profile) {
  Browser* browser = Browser::Create(profile);
  browser->AddBlankTab(true);
  browser->window()->Show();
}

// static
void Browser::OpenWindowWithRestoredTabs(Profile* profile) {
  TabRestoreService* service = profile->GetTabRestoreService();
  if (service)
    service->RestoreMostRecentEntry(NULL);
}

// static
void Browser::OpenURLOffTheRecord(Profile* profile, const GURL& url) {
  Profile* off_the_record_profile = profile->GetOffTheRecordProfile();
  Browser* browser = BrowserList::FindBrowserWithType(
      off_the_record_profile, TYPE_NORMAL, false);
  if (!browser)
    browser = Browser::Create(off_the_record_profile);
  // TODO(eroman): should we have referrer here?
  browser->AddTabWithURL(
      url, GURL(), PageTransition::LINK, -1, TabStripModel::ADD_SELECTED, NULL,
      std::string());
  browser->window()->Show();
}

// static
Browser* Browser::FindAppWindowOrPanel(Profile* profile,
                                       Extension* extension_app) {
  // Test the focused browser first.
  Browser* browser = BrowserList::GetLastActive();
  if (browser && BrowserHostsExtensionApp(browser, profile, extension_app))
    return browser;

  BrowserList::const_iterator browser_all;
  for (browser_all = BrowserList::begin();
       browser_all != BrowserList::end();
       ++browser_all) {
    if (BrowserHostsExtensionApp(*browser_all, profile, extension_app))
      return *browser_all;
  }
  return NULL;
}

// static
TabContents* Browser::FindAppTab(Browser* browser, Extension* extension_app) {
  if (browser->type() != Browser::TYPE_NORMAL)
    return NULL;

  for (int tab_idx = 0; tab_idx < browser->tab_count(); ++tab_idx) {
    TabContents* tab_contents = browser->GetTabContentsAt(tab_idx);
    if (!tab_contents)
      continue;

    if (tab_contents->extension_app() != extension_app)
      continue;

    return tab_contents;
  }

  return NULL;
}

// static
// TODO(erikkay): There are multiple reasons why this could fail.  Should
// this function return an error reason as well so that callers can show
// reasonable errors?
TabContents* Browser::OpenApplication(Profile* profile,
                                      const std::string& app_id) {
  ExtensionsService* extensions_service = profile->GetExtensionsService();
  if (!extensions_service->is_ready())
    return NULL;

  // If the extension with |app_id| could't be found, most likely because it
  // was uninstalled.
  Extension* extension = extensions_service->GetExtensionById(app_id, false);
  if (!extension)
    return NULL;

  return OpenApplication(profile, extension, extension->launch_container());
}

// static
TabContents* Browser::OpenApplication(Profile* profile,
                                      Extension* extension,
                                      Extension::LaunchContainer container) {
  TabContents* tab = NULL;

  // If the app is loaded in an existing window or panel, focus it.
  Browser* browser = FindAppWindowOrPanel(profile, extension);
  if (browser) {
    browser->window()->Show();
    return browser->GetSelectedTabContents();
  }

  // If an app is loaded in an app tab in the focused browser, select it.
  browser = BrowserList::GetLastActive();
  if (browser && browser->profile() == profile) {
    tab = Browser::FindAppTab(browser, extension);
    if (tab) {
      int tab_idx = browser->tabstrip_model()->GetIndexOfTabContents(tab);
      DCHECK(tab_idx != TabStripModel::kNoTab);
      browser->SelectTabContentsAt(tab_idx, false);
      return tab;
    }
  }

  // The app is not yet open.  Load it.
  switch (container) {
    case Extension::LAUNCH_WINDOW:
    case Extension::LAUNCH_PANEL:
      tab = Browser::OpenApplicationWindow(profile, extension, container,
                                           GURL());
      break;
    case Extension::LAUNCH_TAB: {
      tab = Browser::OpenApplicationTab(profile, extension);
      break;
    }
    default:
      NOTREACHED();
      break;
  }
  if (tab) {
    Browser* browser = tab->delegate()->GetBrowser();
    if (browser && extension && extension->launch_fullscreen())
      browser->window()->SetFullscreen(true);
  }
  return tab;
}

// static
TabContents* Browser::OpenApplicationWindow(
    Profile* profile,
    Extension* extension,
    Extension::LaunchContainer container,
    const GURL& url_input) {
  GURL url;
  if (!url_input.is_empty()) {
    if (extension)
      DCHECK(extension->web_extent().ContainsURL(url_input));
    url = url_input;
  } else {
    DCHECK(extension);
    url = extension->GetFullLaunchURL();
  }

  // TODO(erikkay) this can't be correct for extensions
  std::wstring app_name = web_app::GenerateApplicationNameFromURL(url);
  RegisterAppPrefs(app_name);

  bool as_panel = extension && (container == Extension::LAUNCH_PANEL);
  Browser* browser = Browser::CreateForApp(app_name, extension, profile,
                                           as_panel);
  TabContents* tab_contents = browser->AddTabWithURL(
      url, GURL(), PageTransition::START_PAGE, -1, TabStripModel::ADD_SELECTED,
      NULL, std::string());

  tab_contents->GetMutableRendererPrefs()->can_accept_load_drops = false;
  tab_contents->render_view_host()->SyncRendererPrefs();
  browser->window()->Show();

  // TODO(jcampan): http://crbug.com/8123 we should not need to set the initial
  //                focus explicitly.
  tab_contents->view()->SetInitialFocus();

  if (!as_panel) {
    // Set UPDATE_SHORTCUT as the pending web app action. This action is picked
    // up in LoadingStateChanged to schedule a GetApplicationInfo. And when
    // the web app info is available, TabContents notifies Browser via
    // OnDidGetApplicationInfo, which calls
    // web_app::UpdateShortcutForTabContents when it sees UPDATE_SHORTCUT as
    // pending web app action.
    browser->pending_web_app_action_ = UPDATE_SHORTCUT;
  }

  return tab_contents;
}

// static
TabContents* Browser::OpenApplicationWindow(Profile* profile,
                                            GURL& url) {
  return OpenApplicationWindow(profile, NULL, Extension::LAUNCH_WINDOW, url);
}

// static
TabContents* Browser::OpenApplicationTab(Profile* profile,
                                         Extension* extension) {
  Browser* browser = BrowserList::GetLastActiveWithProfile(profile);
  if (!browser || browser->type() != Browser::TYPE_NORMAL)
    return NULL;

  // TODO(erikkay): This doesn't seem like the right transition in all cases.
  PageTransition::Type transition = PageTransition::START_PAGE;
  GURL url = extension->GetFullLaunchURL();
  TabContents* tab_contents =
      browser->CreateTabContentsForURL(url, GURL(), profile,
                                       transition, false, NULL);
  tab_contents->SetExtensionApp(extension);
  browser->AddTab(tab_contents, transition);
  return tab_contents;
}

// static
void Browser::OpenBookmarkManagerWindow(Profile* profile) {
  Browser* browser = Browser::Create(profile);
  browser->ShowBookmarkManagerTab();
  browser->window()->Show();
}

#if defined(OS_MACOSX)
// static
void Browser::OpenHistoryWindow(Profile* profile) {
  Browser* browser = Browser::Create(profile);
  browser->ShowHistoryTab();
  browser->window()->Show();
}

// static
void Browser::OpenDownloadsWindow(Profile* profile) {
  Browser* browser = Browser::Create(profile);
  browser->ShowDownloadsTab();
  browser->window()->Show();
}

// static
void Browser::OpenHelpWindow(Profile* profile) {
  Browser* browser = Browser::Create(profile);
  browser->OpenHelpTab();
  browser->window()->Show();
}

void Browser::OpenOptionsWindow(Profile* profile) {
  Browser* browser = Browser::Create(profile);
  browser->ShowOptionsTab();
  browser->window()->Show();
}
#endif

// static
void Browser::OpenExtensionsWindow(Profile* profile) {
  Browser* browser = Browser::Create(profile);
  browser->ShowExtensionsTab();
  browser->window()->Show();
}


///////////////////////////////////////////////////////////////////////////////
// Browser, State Storage and Retrieval for UI:

std::wstring Browser::GetWindowPlacementKey() const {
  std::wstring name(prefs::kBrowserWindowPlacement);
  if (!app_name_.empty()) {
    name.append(L"_");
    name.append(app_name_);
  }
  return name;
}

bool Browser::ShouldSaveWindowPlacement() const {
  // Only save the window placement of popups if they are restored.
  return (type() & TYPE_POPUP) == 0 || browser_defaults::kRestorePopups;
}

void Browser::SaveWindowPlacement(const gfx::Rect& bounds, bool maximized) {
  // Save to the session storage service, used when reloading a past session.
  // Note that we don't want to be the ones who cause lazy initialization of
  // the session service. This function gets called during initial window
  // showing, and we don't want to bring in the session service this early.
  if (profile()->HasSessionService()) {
    SessionService* session_service = profile()->GetSessionService();
    if (session_service)
      session_service->SetWindowBounds(session_id_, bounds, maximized);
  }
}

gfx::Rect Browser::GetSavedWindowBounds() const {
  const CommandLine& parsed_command_line = *CommandLine::ForCurrentProcess();
  bool record_mode = parsed_command_line.HasSwitch(switches::kRecordMode);
  bool playback_mode = parsed_command_line.HasSwitch(switches::kPlaybackMode);
  if (record_mode || playback_mode) {
    // In playback/record mode we always fix the size of the browser and
    // move it to (0,0).  The reason for this is two reasons:  First we want
    // resize/moves in the playback to still work, and Second we want
    // playbacks to work (as much as possible) on machines w/ different
    // screen sizes.
    return gfx::Rect(0, 0, 800, 600);
  }

  gfx::Rect restored_bounds = override_bounds_;
  bool maximized;
  WindowSizer::GetBrowserWindowBounds(app_name_, restored_bounds, NULL,
                                      &restored_bounds, &maximized);
  return restored_bounds;
}

// TODO(beng): obtain maximized state some other way so we don't need to go
//             through all this hassle.
bool Browser::GetSavedMaximizedState() const {
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kStartMaximized))
    return true;

  if (maximized_state_ == MAXIMIZED_STATE_MAXIMIZED)
    return true;
  if (maximized_state_ == MAXIMIZED_STATE_UNMAXIMIZED)
    return false;

  // An explicit maximized state was not set. Query the window sizer.
  gfx::Rect restored_bounds;
  bool maximized = false;
  WindowSizer::GetBrowserWindowBounds(app_name_, restored_bounds, NULL,
                                      &restored_bounds, &maximized);
  return maximized;
}

SkBitmap Browser::GetCurrentPageIcon() const {
  TabContents* contents = GetSelectedTabContents();
  // |contents| can be NULL since GetCurrentPageIcon() is called by the window
  // during the window's creation (before tabs have been added).
  return contents ? contents->GetFavIcon() : SkBitmap();
}

string16 Browser::GetWindowTitleForCurrentTab() const {
  TabContents* contents = tabstrip_model_.GetSelectedTabContents();
  string16 title;

  // |contents| can be NULL because GetWindowTitleForCurrentTab is called by the
  // window during the window's creation (before tabs have been added).
  if (contents) {
    title = contents->GetTitle();
    FormatTitleForDisplay(&title);
  }
  if (title.empty())
    title = TabContents::GetDefaultTitle();

#if defined(OS_MACOSX) || defined(OS_CHROMEOS)
  // On Mac or ChromeOS, we don't want to suffix the page title with
  // the application name.
  return title;
#elif defined(OS_WIN) || defined(OS_LINUX)
  int string_id = IDS_BROWSER_WINDOW_TITLE_FORMAT;
  // Don't append the app name to window titles on app frames and app popups
  if (type_ & TYPE_APP)
    string_id = IDS_BROWSER_WINDOW_TITLE_FORMAT_NO_LOGO;
  return l10n_util::GetStringFUTF16(string_id, title);
#endif
}

// static
void Browser::FormatTitleForDisplay(string16* title) {
  size_t current_index = 0;
  size_t match_index;
  while ((match_index = title->find(L'\n', current_index)) !=
         std::wstring::npos) {
    title->replace(match_index, 1, string16());
    current_index = match_index;
  }
}

///////////////////////////////////////////////////////////////////////////////
// Browser, OnBeforeUnload handling:

bool Browser::ShouldCloseWindow() {
  if (!CanCloseWithInProgressDownloads())
    return false;

  if (HasCompletedUnloadProcessing())
    return IsClosingPermitted();

  is_attempting_to_close_browser_ = true;

  for (int i = 0; i < tab_count(); ++i) {
    TabContents* contents = GetTabContentsAt(i);
    if (TabHasUnloadListener(contents))
      tabs_needing_before_unload_fired_.insert(contents);
  }

  if (tabs_needing_before_unload_fired_.empty())
    return IsClosingPermitted();

  ProcessPendingTabs();
  return false;
}

void Browser::OnWindowClosing() {
  if (!ShouldCloseWindow())
    return;

  bool exiting = false;

  // Application should shutdown on last window close if the user is explicitly
  // trying to quit, or if there is nothing keeping the browser alive (such as
  // AppController on the Mac, or BackgroundContentsService for background
  // pages).
  bool should_quit_if_last_browser =
      browser_shutdown::IsTryingToQuit() || !BrowserList::WillKeepAlive();

  if (should_quit_if_last_browser && BrowserList::size() == 1) {
    browser_shutdown::OnShutdownStarting(browser_shutdown::WINDOW_CLOSE);
    exiting = true;
  }

  // Don't use HasSessionService here, we want to force creation of the
  // session service so that user can restore what was open.
  SessionService* session_service = profile()->GetSessionService();
  if (session_service)
    session_service->WindowClosing(session_id());

  TabRestoreService* tab_restore_service = profile()->GetTabRestoreService();
  if (tab_restore_service)
    tab_restore_service->BrowserClosing(this);

  // TODO(sky): convert session/tab restore to use notification.
  NotificationService::current()->Notify(
      NotificationType::BROWSER_CLOSING,
      Source<Browser>(this),
      Details<bool>(&exiting));

  CloseAllTabs();
}

////////////////////////////////////////////////////////////////////////////////
// In-progress download termination handling:

void Browser::InProgressDownloadResponse(bool cancel_downloads) {
  if (cancel_downloads) {
    cancel_download_confirmation_state_ = RESPONSE_RECEIVED;
    CloseWindow();
    return;
  }

  // Sets the confirmation state to NOT_PROMPTED so that if the user tries to
  // close again we'll show the warning again.
  cancel_download_confirmation_state_ = NOT_PROMPTED;

  // Show the download page so the user can figure-out what downloads are still
  // in-progress.
  ShowDownloadsTab();
}

////////////////////////////////////////////////////////////////////////////////
// Browser, Tab adding/showing functions:

int Browser::GetIndexForInsertionDuringRestore(int relative_index) {
  return (tabstrip_model_.insertion_policy() == TabStripModel::INSERT_AFTER) ?
      tab_count() : relative_index;
}

TabContents* Browser::AddTabWithURL(const GURL& url,
                                    const GURL& referrer,
                                    PageTransition::Type transition,
                                    int index,
                                    int add_types,
                                    SiteInstance* instance,
                                    const std::string& extension_app_id) {
  TabContents* contents = NULL;
  if (CanSupportWindowFeature(FEATURE_TABSTRIP) || tabstrip_model()->empty()) {
    GURL url_to_load = url;
    if (url_to_load.is_empty())
      url_to_load = GetHomePage();
    contents = CreateTabContentsForURL(url_to_load, referrer, profile_,
                                       transition, false, instance);
    contents->SetExtensionAppById(extension_app_id);
    tabstrip_model_.AddTabContents(contents, index, transition, add_types);
    // TODO(sky): figure out why this is needed. Without it we seem to get
    // failures in startup tests.
    // By default, content believes it is not hidden.  When adding contents
    // in the background, tell it that it's hidden.
    if ((add_types & TabStripModel::ADD_SELECTED) == 0) {
      // TabStripModel::AddTabContents invokes HideContents if not foreground.
      contents->WasHidden();
    }
  } else {
    // We're in an app window or a popup window. Find an existing browser to
    // open this URL in, creating one if none exists.
    Browser* b = BrowserList::FindBrowserWithFeature(profile_,
                                                     FEATURE_TABSTRIP);
    if (!b)
      b = Browser::Create(profile_);
    contents = b->AddTabWithURL(url, referrer, transition, index, add_types,
                                instance, extension_app_id);
    b->window()->Show();
  }
  return contents;
}

TabContents* Browser::AddTab(TabContents* tab_contents,
                             PageTransition::Type type) {
  tabstrip_model_.AddTabContents(
      tab_contents, -1, type, TabStripModel::ADD_SELECTED);
  return tab_contents;
}

void Browser::AddTabContents(TabContents* new_contents,
                             WindowOpenDisposition disposition,
                             const gfx::Rect& initial_pos,
                             bool user_gesture) {
  AddNewContents(NULL, new_contents, disposition, initial_pos, user_gesture);
}

void Browser::CloseTabContents(TabContents* contents) {
  CloseContents(contents);
}

void Browser::BrowserShowHtmlDialog(HtmlDialogUIDelegate* delegate,
                                    gfx::NativeWindow parent_window) {
  ShowHtmlDialog(delegate, parent_window);
}

void Browser::BrowserRenderWidgetShowing() {
  RenderWidgetShowing();
}

void Browser::ToolbarSizeChanged(bool is_animating) {
  ToolbarSizeChanged(NULL, is_animating);
}

TabContents* Browser::AddRestoredTab(
    const std::vector<TabNavigation>& navigations,
    int tab_index,
    int selected_navigation,
    const std::string& extension_app_id,
    bool select,
    bool pin,
    bool from_last_session) {
  TabContents* new_tab = new TabContents(profile(), NULL,
      MSG_ROUTING_NONE, tabstrip_model_.GetSelectedTabContents());
  new_tab->SetExtensionAppById(extension_app_id);
  new_tab->controller().RestoreFromState(navigations, selected_navigation,
                                         from_last_session);

  bool really_pin =
      (pin && tab_index == tabstrip_model()->IndexOfFirstNonMiniTab());
  tabstrip_model_.InsertTabContentsAt(
      tab_index, new_tab,
      select ? TabStripModel::ADD_SELECTED : TabStripModel::ADD_NONE);
  if (really_pin)
    tabstrip_model_.SetTabPinned(tab_index, true);
  if (select) {
    window_->Activate();
  } else {
    // We set the size of the view here, before WebKit does its initial
    // layout.  If we don't, the initial layout of background tabs will be
    // performed with a view width of 0, which may cause script outputs and
    // anchor link location calculations to be incorrect even after a new
    // layout with proper view dimensions. TabStripModel::AddTabContents()
    // contains similar logic.
    new_tab->view()->SizeContents(window_->GetRestoredBounds().size());
    new_tab->HideContents();
  }
  if (profile_->HasSessionService()) {
    SessionService* session_service = profile_->GetSessionService();
    if (session_service)
      session_service->TabRestored(&new_tab->controller(), really_pin);
  }
  return new_tab;
}

void Browser::ReplaceRestoredTab(
    const std::vector<TabNavigation>& navigations,
    int selected_navigation,
    bool from_last_session,
    const std::string& extension_app_id) {
  TabContents* replacement = new TabContents(profile(), NULL,
      MSG_ROUTING_NONE, tabstrip_model_.GetSelectedTabContents());
  replacement->SetExtensionAppById(extension_app_id);
  replacement->controller().RestoreFromState(navigations, selected_navigation,
                                             from_last_session);

  tabstrip_model_.ReplaceNavigationControllerAt(
      tabstrip_model_.selected_index(),
      &replacement->controller());
}

bool Browser::CanRestoreTab() {
  return command_updater_.IsCommandEnabled(IDC_RESTORE_TAB);
}

bool Browser::NavigateToIndexWithDisposition(int index,
                                             WindowOpenDisposition disp) {
  NavigationController& controller =
      GetOrCloneTabForDisposition(disp)->controller();
  if (index < 0 || index >= controller.entry_count())
    return false;
  controller.GoToIndex(index);
  return true;
}

void Browser::ShowSingletonTab(const GURL& url) {
  // In case the URL was rewritten by the BrowserURLHandler we need to ensure
  // that we do not open another URL that will get redirected to the rewritten
  // URL.
  GURL rewritten_url(url);
  bool reverse_on_redirect = false;
  BrowserURLHandler::RewriteURLIfNecessary(&rewritten_url, profile_,
                                           &reverse_on_redirect);

  // See if we already have a tab with the given URL and select it if so.
  for (int i = 0; i < tabstrip_model_.count(); i++) {
    TabContents* tc = tabstrip_model_.GetTabContentsAt(i);
    if (CompareURLsIgnoreRef(tc->GetURL(), url) ||
        CompareURLsIgnoreRef(tc->GetURL(), rewritten_url)) {
      tabstrip_model_.SelectTabContentsAt(i, false);
      return;
    }
  }

  // Otherwise, just create a new tab.
  AddTabWithURL(url, GURL(), PageTransition::AUTO_BOOKMARK, -1,
                TabStripModel::ADD_SELECTED, NULL, std::string());
}

void Browser::UpdateCommandsForFullscreenMode(bool is_fullscreen) {
#if !defined(OS_MACOSX)
  const bool show_main_ui = (type() == TYPE_NORMAL) && !is_fullscreen;
#else
  const bool show_main_ui = (type() == TYPE_NORMAL);
#endif

  bool main_not_fullscreen_or_popup =
      show_main_ui && !is_fullscreen && (type() & TYPE_POPUP) == 0;

  // Navigation commands
  command_updater_.UpdateCommandEnabled(IDC_OPEN_CURRENT_URL, show_main_ui);

  // Window management commands
  command_updater_.UpdateCommandEnabled(IDC_SHOW_AS_TAB,
      (type() & TYPE_POPUP) && !is_fullscreen);

  // Focus various bits of UI
  command_updater_.UpdateCommandEnabled(IDC_FOCUS_TOOLBAR, show_main_ui);
  command_updater_.UpdateCommandEnabled(IDC_FOCUS_LOCATION, show_main_ui);
  command_updater_.UpdateCommandEnabled(IDC_FOCUS_SEARCH, show_main_ui);
  command_updater_.UpdateCommandEnabled(
      IDC_FOCUS_MENU_BAR, main_not_fullscreen_or_popup);
  command_updater_.UpdateCommandEnabled(
      IDC_FOCUS_NEXT_PANE, main_not_fullscreen_or_popup);
  command_updater_.UpdateCommandEnabled(
      IDC_FOCUS_PREVIOUS_PANE, main_not_fullscreen_or_popup);
  command_updater_.UpdateCommandEnabled(
      IDC_FOCUS_BOOKMARKS, main_not_fullscreen_or_popup);
  command_updater_.UpdateCommandEnabled(
      IDC_FOCUS_CHROMEOS_STATUS, main_not_fullscreen_or_popup);

  // Show various bits of UI
  command_updater_.UpdateCommandEnabled(IDC_DEVELOPER_MENU, show_main_ui);
  command_updater_.UpdateCommandEnabled(IDC_REPORT_BUG, show_main_ui);
  command_updater_.UpdateCommandEnabled(IDC_SHOW_BOOKMARK_BAR, show_main_ui);
  command_updater_.UpdateCommandEnabled(IDC_IMPORT_SETTINGS, show_main_ui);
  command_updater_.UpdateCommandEnabled(IDC_SYNC_BOOKMARKS,
      show_main_ui && profile_->IsSyncAccessible());
  command_updater_.UpdateCommandEnabled(IDC_OPTIONS, show_main_ui);
  command_updater_.UpdateCommandEnabled(IDC_EDIT_SEARCH_ENGINES, show_main_ui);
  command_updater_.UpdateCommandEnabled(IDC_VIEW_PASSWORDS, show_main_ui);
  command_updater_.UpdateCommandEnabled(IDC_ABOUT, show_main_ui);
  command_updater_.UpdateCommandEnabled(IDC_SHOW_APP_MENU, show_main_ui);
  command_updater_.UpdateCommandEnabled(IDC_TOGGLE_VERTICAL_TABS, show_main_ui);
}

bool Browser::OpenAppsPanelAsNewTab() {
#if defined(OS_CHROMEOS) || defined(OS_WIN)
  CommandLine* command_line = CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kAppsPanel)) {
    AppLauncher::ShowForNewTab(this, std::string());
    return true;
  }
#endif
  return false;
}

///////////////////////////////////////////////////////////////////////////////
// Browser, Assorted browser commands:

bool Browser::ShouldOpenNewTabForWindowDisposition(
    WindowOpenDisposition disposition) {
  return (disposition == NEW_FOREGROUND_TAB ||
          disposition == NEW_BACKGROUND_TAB);
}

TabContents* Browser::GetOrCloneTabForDisposition(
       WindowOpenDisposition disposition) {
  TabContents* current_tab = GetSelectedTabContents();
  if (ShouldOpenNewTabForWindowDisposition(disposition)) {
    current_tab = current_tab->Clone();
    tabstrip_model_.AddTabContents(
        current_tab, -1, PageTransition::LINK,
        disposition == NEW_FOREGROUND_TAB ? TabStripModel::ADD_SELECTED :
                                            TabStripModel::ADD_NONE);
  }
  return current_tab;
}

void Browser::UpdateTabStripModelInsertionPolicy() {
  tabstrip_model_.SetInsertionPolicy(UseVerticalTabs() ?
      TabStripModel::INSERT_BEFORE : TabStripModel::INSERT_AFTER);
}

void Browser::UseVerticalTabsChanged() {
  UpdateTabStripModelInsertionPolicy();
  window()->ToggleTabStripMode();
}

bool Browser::SupportsWindowFeatureImpl(WindowFeature feature,
                                        bool check_fullscreen) const {
  // On Mac, fullscreen mode has most normal things (in a slide-down panel). On
  // other platforms, we hide some controls when in fullscreen mode.
  bool hide_ui_for_fullscreen = false;
#if !defined(OS_MACOSX)
  hide_ui_for_fullscreen = check_fullscreen && window_ &&
      window_->IsFullscreen();
#endif

  unsigned int features = FEATURE_INFOBAR;

#if !defined(OS_CHROMEOS)
  // Chrome OS opens a FileBrowse pop up instead of using download shelf.
  // So FEATURE_DOWNLOADSHELF is only added for non-chromeos platforms.
  features |= FEATURE_DOWNLOADSHELF;
#endif  // !defined(OS_CHROMEOS)

  if (type() == TYPE_NORMAL) {
    features |= FEATURE_BOOKMARKBAR;
    features |= FEATURE_EXTENSIONSHELF;
  }

  if (!hide_ui_for_fullscreen) {
    if (type() != TYPE_NORMAL && type() != TYPE_EXTENSION_APP)
      features |= FEATURE_TITLEBAR;

    if (type() == TYPE_NORMAL || type() == TYPE_EXTENSION_APP)
      features |= FEATURE_TABSTRIP;

    if (type() == TYPE_NORMAL || type() == TYPE_EXTENSION_APP)
      features |= FEATURE_TOOLBAR;

    if (type() != TYPE_EXTENSION_APP && (type() & Browser::TYPE_APP) == 0)
      features |= FEATURE_LOCATIONBAR;
  }
  return !!(features & feature);
}

bool Browser::IsClosingPermitted() {
  TabCloseableStateWatcher* watcher =
      g_browser_process->tab_closeable_state_watcher();
  bool can_close = !watcher || watcher->CanCloseBrowser(this);
  if (!can_close && is_attempting_to_close_browser_)
    CancelWindowClose();
  return can_close;
}

void Browser::GoBack(WindowOpenDisposition disposition) {
  UserMetrics::RecordAction(UserMetricsAction("Back"), profile_);

  TabContents* current_tab = GetSelectedTabContents();
  if (current_tab->controller().CanGoBack()) {
    TabContents* new_tab = GetOrCloneTabForDisposition(disposition);
    // If we are on an interstitial page and clone the tab, it won't be copied
    // to the new tab, so we don't need to go back.
    if (current_tab->showing_interstitial_page() && (new_tab != current_tab))
      return;
    new_tab->controller().GoBack();
  }
}

void Browser::GoForward(WindowOpenDisposition disposition) {
  UserMetrics::RecordAction(UserMetricsAction("Forward"), profile_);
  if (GetSelectedTabContents()->controller().CanGoForward())
    GetOrCloneTabForDisposition(disposition)->controller().GoForward();
}

void Browser::Reload(WindowOpenDisposition disposition) {
  UserMetrics::RecordAction(UserMetricsAction("Reload"), profile_);
  ReloadInternal(disposition, false);
}

void Browser::ReloadIgnoringCache(WindowOpenDisposition disposition) {
  UserMetrics::RecordAction(UserMetricsAction("ReloadIgnoringCache"), profile_);
  ReloadInternal(disposition, true);
}

void Browser::ReloadInternal(WindowOpenDisposition disposition,
                             bool ignore_cache) {
  // If we are showing an interstitial, treat this as an OpenURL.
  TabContents* current_tab = GetSelectedTabContents();
  if (current_tab && current_tab->showing_interstitial_page()) {
    NavigationEntry* entry = current_tab->controller().GetActiveEntry();
    DCHECK(entry);  // Should exist if interstitial is showing.
    OpenURL(entry->url(), GURL(), disposition, PageTransition::RELOAD);
    return;
  }

  // As this is caused by a user action, give the focus to the page.
  current_tab = GetOrCloneTabForDisposition(disposition);
  if (!current_tab->FocusLocationBarByDefault())
    current_tab->Focus();
  if (ignore_cache)
    current_tab->controller().ReloadIgnoringCache(true);
  else
    current_tab->controller().Reload(true);
}

void Browser::Home(WindowOpenDisposition disposition) {
  UserMetrics::RecordAction(UserMetricsAction("Home"), profile_);
  OpenURL(GetHomePage(), GURL(), disposition, PageTransition::AUTO_BOOKMARK);
}

void Browser::OpenCurrentURL() {
  UserMetrics::RecordAction(UserMetricsAction("LoadURL"), profile_);
  LocationBar* location_bar = window_->GetLocationBar();
  WindowOpenDisposition open_disposition =
      location_bar->GetWindowOpenDisposition();
  GURL url(WideToUTF8(location_bar->GetInputString()));

  if (open_disposition == CURRENT_TAB) {
    TabContents* selected_contents = GetSelectedTabContents();
    Extension* extension = profile()->GetExtensionsService()
        ->GetExtensionByWebExtent(url);

    if (extension && selected_contents &&
        selected_contents->GetURL().GetOrigin() ==
            GURL(chrome::kChromeUINewTabURL).GetOrigin()) {
      // If the |url| is within an app's web extent and it was typed into the
      // omnibox of an NTP page, interpret as an app launch and close the NTP
      // tab.
      Browser::OpenApplication(profile(), extension,
          extension->launch_container());
      CloseTabContents(selected_contents);
      return;
    }

    if (selected_contents) {
      // For the purposes of changing the window open disposition, the referrer
      // is the current tab's URL.
      open_disposition = AdjustWindowOpenDispositionForTab(
          IsPinned(selected_contents),
          url,
          selected_contents->GetURL(),
          location_bar->GetPageTransition(),
          open_disposition);
    }
  }

  // Use ADD_INHERIT_OPENER so that all pages opened by the omnibox at least
  // inherit the opener. In some cases the tabstrip will determine the group
  // should be inherited, in which case the group is inherited instead of the
  // opener.
  OpenURLAtIndex(NULL, url, GURL(), open_disposition,
                 location_bar->GetPageTransition(), -1,
                 TabStripModel::ADD_FORCE_INDEX |
                     TabStripModel::ADD_INHERIT_OPENER);
}

void Browser::Stop() {
  UserMetrics::RecordAction(UserMetricsAction("Stop"), profile_);
  GetSelectedTabContents()->Stop();
}

void Browser::NewWindow() {
  if (browser_defaults::kAlwaysOpenIncognitoWindow &&
      CommandLine::ForCurrentProcess()->HasSwitch(switches::kIncognito)) {
    NewIncognitoWindow();
    return;
  }
  UserMetrics::RecordAction(UserMetricsAction("NewWindow"), profile_);
  SessionService* session_service =
      profile_->GetOriginalProfile()->GetSessionService();
  if (!session_service ||
      !session_service->RestoreIfNecessary(std::vector<GURL>())) {
    Browser::OpenEmptyWindow(profile_->GetOriginalProfile());
  }
}

void Browser::NewIncognitoWindow() {
  UserMetrics::RecordAction(UserMetricsAction("NewIncognitoWindow"), profile_);
  Browser::OpenEmptyWindow(profile_->GetOffTheRecordProfile());
}

void Browser::CloseWindow() {
  UserMetrics::RecordAction(UserMetricsAction("CloseWindow"), profile_);
  window_->Close();
}

void Browser::NewTab() {
  UserMetrics::RecordAction(UserMetricsAction("NewTab"), profile_);

  if (OpenAppsPanelAsNewTab())
    return;

  if (type() == TYPE_NORMAL) {
    AddBlankTab(true);
  } else {
    Browser* b = GetOrCreateTabbedBrowser(profile_);
    b->AddBlankTab(true);
    b->window()->Show();
    // The call to AddBlankTab above did not set the focus to the tab as its
    // window was not active, so we have to do it explicitly.
    // See http://crbug.com/6380.
    b->GetSelectedTabContents()->view()->RestoreFocus();
  }
}

void Browser::CloseTab() {
  UserMetrics::RecordAction(UserMetricsAction("CloseTab_Accelerator"),
                            profile_);
  if (CanCloseTab()) {
    tabstrip_model_.CloseTabContentsAt(
        tabstrip_model_.selected_index(),
        TabStripModel::CLOSE_USER_GESTURE |
        TabStripModel::CLOSE_CREATE_HISTORICAL_TAB);
  }
}

void Browser::SelectNextTab() {
  UserMetrics::RecordAction(UserMetricsAction("SelectNextTab"), profile_);
  tabstrip_model_.SelectNextTab();
}

void Browser::SelectPreviousTab() {
  UserMetrics::RecordAction(UserMetricsAction("SelectPrevTab"), profile_);
  tabstrip_model_.SelectPreviousTab();
}

void Browser::MoveTabNext() {
  UserMetrics::RecordAction(UserMetricsAction("MoveTabNext"), profile_);
  tabstrip_model_.MoveTabNext();
}

void Browser::MoveTabPrevious() {
  UserMetrics::RecordAction(UserMetricsAction("MoveTabPrevious"), profile_);
  tabstrip_model_.MoveTabPrevious();
}

void Browser::SelectNumberedTab(int index) {
  if (index < tab_count()) {
    UserMetrics::RecordAction(UserMetricsAction("SelectNumberedTab"),
                              profile_);
    tabstrip_model_.SelectTabContentsAt(index, true);
  }
}

void Browser::SelectLastTab() {
  UserMetrics::RecordAction(UserMetricsAction("SelectLastTab"), profile_);
  tabstrip_model_.SelectLastTab();
}

void Browser::DuplicateTab() {
  UserMetrics::RecordAction(UserMetricsAction("Duplicate"), profile_);
  DuplicateContentsAt(selected_index());
}

void Browser::RestoreTab() {
  UserMetrics::RecordAction(UserMetricsAction("RestoreTab"), profile_);
  TabRestoreService* service = profile_->GetTabRestoreService();
  if (!service)
    return;

  service->RestoreMostRecentEntry(this);
}

void Browser::WriteCurrentURLToClipboard() {
  // TODO(ericu): There isn't currently a metric for this.  Should there be?
  // We don't appear to track the action when it comes from the
  // RenderContextViewMenu.

  TabContents* contents = GetSelectedTabContents();
  if (!contents->ShouldDisplayURL())
    return;

  chrome_browser_net::WriteURLToClipboard(
      contents->GetURL(),
      UTF8ToWide(profile_->GetPrefs()->GetString(prefs::kAcceptLanguages)),
      g_browser_process->clipboard());
}

void Browser::ConvertPopupToTabbedBrowser() {
  UserMetrics::RecordAction(UserMetricsAction("ShowAsTab"), profile_);
  int tab_strip_index = tabstrip_model_.selected_index();
  TabContents* contents = tabstrip_model_.DetachTabContentsAt(tab_strip_index);
  Browser* browser = Browser::Create(profile_);
  browser->tabstrip_model()->AppendTabContents(contents, true);
  browser->window()->Show();
}

void Browser::ToggleFullscreenMode() {
#if !defined(OS_MACOSX)
  // In kiosk mode, we always want to be fullscreen. When the browser first
  // starts we're not yet fullscreen, so let the initial toggle go through.
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kKioskMode) &&
      window_->IsFullscreen())
    return;
#endif

  UserMetrics::RecordAction(UserMetricsAction("ToggleFullscreen"), profile_);
  window_->SetFullscreen(!window_->IsFullscreen());
  // On Linux, setting fullscreen mode is an async call to the X server, which
  // may or may not support fullscreen mode.
#if !defined(OS_LINUX)
  UpdateCommandsForFullscreenMode(window_->IsFullscreen());
#endif
}

#if defined(OS_CHROMEOS)
void Browser::Search() {
  // If the NTP is showing, close it.
  if (StartsWithASCII(GetSelectedTabContents()->GetURL().spec(),
                       chrome::kChromeUINewTabURL, true)) {
    CloseTab();
    return;
  }
  // Otherwise just open it.
  NewTab();
}
#endif

void Browser::Exit() {
  UserMetrics::RecordAction(UserMetricsAction("Exit"), profile_);
  BrowserList::CloseAllBrowsersAndExit();
}

void Browser::BookmarkCurrentPage() {
  UserMetrics::RecordAction(UserMetricsAction("Star"), profile_);

  BookmarkModel* model = profile()->GetBookmarkModel();
  if (!model || !model->IsLoaded())
    return;  // Ignore requests until bookmarks are loaded.

  GURL url;
  std::wstring title;
  bookmark_utils::GetURLAndTitleToBookmark(GetSelectedTabContents(), &url,
                                           &title);
  bool was_bookmarked = model->IsBookmarked(url);
  model->SetURLStarred(url, title, true);
  // Make sure the model actually added a bookmark before showing the star. A
  // bookmark isn't created if the url is invalid.
  if (window_->IsActive() && model->IsBookmarked(url)) {
    // Only show the bubble if the window is active, otherwise we may get into
    // weird situations were the bubble is deleted as soon as it is shown.
    window_->ShowBookmarkBubble(url, was_bookmarked);
  }
}

void Browser::SavePage() {
  UserMetrics::RecordAction(UserMetricsAction("SavePage"), profile_);
  GetSelectedTabContents()->OnSavePage();
}

void Browser::ViewSource() {
  UserMetrics::RecordAction(UserMetricsAction("ViewSource"), profile_);

  TabContents* current_tab = GetSelectedTabContents();
  NavigationEntry* entry = current_tab->controller().GetLastCommittedEntry();
  if (entry) {
    OpenURL(GURL(chrome::kViewSourceScheme + std::string(":") +
        entry->url().spec()), GURL(), NEW_FOREGROUND_TAB, PageTransition::LINK);
  }
}

void Browser::ShowFindBar() {
  GetFindBarController()->Show();
}

bool Browser::SupportsWindowFeature(WindowFeature feature) const {
  return SupportsWindowFeatureImpl(feature, true);
}

bool Browser::CanSupportWindowFeature(WindowFeature feature) const {
  return SupportsWindowFeatureImpl(feature, false);
}

void Browser::EmailPageLocation() {
  UserMetrics::RecordAction(UserMetricsAction("EmailPageLocation"), profile_);
  GetSelectedTabContents()->EmailPageLocation();
}

void Browser::Print() {
  UserMetrics::RecordAction(UserMetricsAction("PrintPreview"), profile_);
  GetSelectedTabContents()->PrintPreview();
}

void Browser::ToggleEncodingAutoDetect() {
  UserMetrics::RecordAction(UserMetricsAction("AutoDetectChange"), profile_);
  encoding_auto_detect_.SetValue(!encoding_auto_detect_.GetValue());
  // If "auto detect" is turned on, then any current override encoding
  // is cleared. This also implicitly performs a reload.
  // OTOH, if "auto detect" is turned off, we don't change the currently
  // active encoding.
  if (encoding_auto_detect_.GetValue()) {
    TabContents* contents = GetSelectedTabContents();
    if (contents)
      contents->ResetOverrideEncoding();
  }
}

void Browser::OverrideEncoding(int encoding_id) {
  UserMetrics::RecordAction(UserMetricsAction("OverrideEncoding"), profile_);
  const std::string selected_encoding =
      CharacterEncoding::GetCanonicalEncodingNameByCommandId(encoding_id);
  TabContents* contents = GetSelectedTabContents();
  if (!selected_encoding.empty() && contents)
     contents->SetOverrideEncoding(selected_encoding);
  // Update the list of recently selected encodings.
  std::string new_selected_encoding_list;
  if (CharacterEncoding::UpdateRecentlySelectedEncoding(
        profile_->GetPrefs()->GetString(prefs::kRecentlySelectedEncoding),
        encoding_id,
        &new_selected_encoding_list)) {
    profile_->GetPrefs()->SetString(prefs::kRecentlySelectedEncoding,
                                    new_selected_encoding_list);
  }
}

void Browser::Cut() {
  UserMetrics::RecordAction(UserMetricsAction("Cut"), profile_);
  window()->Cut();
}

void Browser::Copy() {
  UserMetrics::RecordAction(UserMetricsAction("Copy"), profile_);
  window()->Copy();
}

void Browser::Paste() {
  UserMetrics::RecordAction(UserMetricsAction("Paste"), profile_);
  window()->Paste();
}

void Browser::Find() {
  UserMetrics::RecordAction(UserMetricsAction("Find"), profile_);
  FindInPage(false, false);
}

void Browser::FindNext() {
  UserMetrics::RecordAction(UserMetricsAction("FindNext"), profile_);
  FindInPage(true, true);
}

void Browser::FindPrevious() {
  UserMetrics::RecordAction(UserMetricsAction("FindPrevious"), profile_);
  FindInPage(true, false);
}

void Browser::Zoom(PageZoom::Function zoom_function) {
  static const UserMetricsAction kActions[] = {
                      UserMetricsAction("ZoomMinus"),
                      UserMetricsAction("ZoomNormal"),
                      UserMetricsAction("ZoomPlus")
                      };

  UserMetrics::RecordAction(kActions[zoom_function - PageZoom::ZOOM_OUT],
                            profile_);
  TabContents* tab_contents = GetSelectedTabContents();
  tab_contents->render_view_host()->Zoom(zoom_function);
}

void Browser::FocusToolbar() {
  UserMetrics::RecordAction(UserMetricsAction("FocusToolbar"), profile_);
  window_->FocusToolbar();
}

void Browser::FocusAppMenu() {
  UserMetrics::RecordAction(UserMetricsAction("FocusAppMenu"), profile_);
  window_->FocusAppMenu();
}

void Browser::FocusLocationBar() {
  UserMetrics::RecordAction(UserMetricsAction("FocusLocation"), profile_);
  window_->SetFocusToLocationBar(true);
}

void Browser::FocusBookmarksToolbar() {
  UserMetrics::RecordAction(UserMetricsAction("FocusBookmarksToolbar"),
                            profile_);
  window_->FocusBookmarksToolbar();
}

void Browser::FocusChromeOSStatus() {
  UserMetrics::RecordAction(UserMetricsAction("FocusChromeOSStatus"), profile_);
  window_->FocusChromeOSStatus();
}

void Browser::FocusNextPane() {
  UserMetrics::RecordAction(UserMetricsAction("FocusNextPane"), profile_);
  window_->RotatePaneFocus(true);
}

void Browser::FocusPreviousPane() {
  UserMetrics::RecordAction(UserMetricsAction("FocusPreviousPane"), profile_);
  window_->RotatePaneFocus(false);
}

void Browser::FocusSearch() {
  // TODO(beng): replace this with FocusLocationBar
  UserMetrics::RecordAction(UserMetricsAction("FocusSearch"), profile_);
  window_->GetLocationBar()->FocusSearch();
}

void Browser::OpenFile() {
  UserMetrics::RecordAction(UserMetricsAction("OpenFile"), profile_);
#if defined(OS_CHROMEOS)
  FileBrowseUI::OpenPopup(profile_,
                          "",
                          FileBrowseUI::kPopupWidth,
                          FileBrowseUI::kPopupHeight);
#else
  if (!select_file_dialog_.get())
    select_file_dialog_ = SelectFileDialog::Create(this);

  const FilePath directory = profile_->last_selected_directory();

  // TODO(beng): figure out how to juggle this.
  gfx::NativeWindow parent_window = window_->GetNativeHandle();
  select_file_dialog_->SelectFile(SelectFileDialog::SELECT_OPEN_FILE,
                                  string16(), directory,
                                  NULL, 0, FILE_PATH_LITERAL(""),
                                  parent_window, NULL);
#endif
}

void Browser::OpenCreateShortcutsDialog() {
  UserMetrics::RecordAction(UserMetricsAction("CreateShortcut"), profile_);
#if defined(OS_WIN) || defined(OS_LINUX)
  TabContents* current_tab = GetSelectedTabContents();
  DCHECK(current_tab && web_app::IsValidUrl(current_tab->GetURL())) <<
      "Menu item should be disabled.";

  NavigationEntry* entry = current_tab->controller().GetLastCommittedEntry();
  if (!entry)
    return;

  // RVH's GetApplicationInfo should not be called before it returns.
  DCHECK(pending_web_app_action_ == NONE);
  pending_web_app_action_ = CREATE_SHORTCUT;

  // Start fetching web app info for CreateApplicationShortcut dialog and show
  // the dialog when the data is available in OnDidGetApplicationInfo.
  current_tab->render_view_host()->GetApplicationInfo(entry->page_id());
#else
  NOTIMPLEMENTED();
#endif
}

void Browser::ToggleDevToolsWindow(DevToolsToggleAction action) {
  std::string uma_string;
  switch (action) {
    case DEVTOOLS_TOGGLE_ACTION_SHOW_CONSOLE:
      uma_string = "DevTools_ToggleConsole";
      break;
    case DEVTOOLS_TOGGLE_ACTION_NONE:
    case DEVTOOLS_TOGGLE_ACTION_INSPECT:
    default:
      uma_string = "DevTools_ToggleWindow";
      break;
  }
  UserMetrics::RecordAction(UserMetricsAction(uma_string.c_str()), profile_);
  DevToolsManager::GetInstance()->ToggleDevToolsWindow(
      GetSelectedTabContents()->render_view_host(), action);
}

void Browser::OpenTaskManager() {
  UserMetrics::RecordAction(UserMetricsAction("TaskManager"), profile_);
  window_->ShowTaskManager();
}

void Browser::OpenBugReportDialog() {
#if defined(OS_CHROMEOS)
  UserMetrics::RecordAction(UserMetricsAction("ReportBug"), profile_);
  window_->ShowReportBugDialog();
#else
  TabContents* contents = GetSelectedTabContents();
  if (!contents)
    return;
  ShowBrokenPageTab(contents);
#endif
}

void Browser::ToggleBookmarkBar() {
  UserMetrics::RecordAction(UserMetricsAction("ShowBookmarksBar"), profile_);
  window_->ToggleBookmarkBar();
}

void Browser::ToggleExtensionShelf() {
  UserMetrics::RecordAction(UserMetricsAction("ToggleExtensionShelf"),
                            profile_);
  window_->ToggleExtensionShelf();
}

void Browser::OpenBookmarkManager() {
  UserMetrics::RecordAction(UserMetricsAction("ShowBookmarkManager"), profile_);
  ShowBookmarkManagerTab();
}

void Browser::ShowAppMenu() {
  UserMetrics::RecordAction(UserMetricsAction("ShowAppMenu"), profile_);
  window_->ShowAppMenu();
}

void Browser::ShowBookmarkManagerTab() {
  // The bookmark manager tab does not work in incognito mode. If we are OTR
  // we try to reuse the last active window and if that fails we open a new
  // window.
  Profile* default_profile = profile_->GetOriginalProfile();
  UserMetrics::RecordAction(UserMetricsAction("ShowBookmarks"),
                            default_profile);

  if (!profile_->IsOffTheRecord()) {
    ShowSingletonTab(GURL(chrome::kChromeUIBookmarksURL));
  } else {
    Browser* browser = BrowserList::GetLastActiveWithProfile(default_profile);
    if (browser) {
      browser->ShowBookmarkManagerTab();
      browser->window()->Activate();
    } else {
      OpenBookmarkManagerWindow(default_profile);
    }
  }
}

void Browser::ShowHistoryTab() {
  UserMetrics::RecordAction(UserMetricsAction("ShowHistory"), profile_);
  ShowSingletonTab(GURL(chrome::kChromeUIHistoryURL));
}

void Browser::ShowDownloadsTab() {
  UserMetrics::RecordAction(UserMetricsAction("ShowDownloads"), profile_);
  ShowSingletonTab(GURL(chrome::kChromeUIDownloadsURL));
}

void Browser::ShowExtensionsTab() {
  UserMetrics::RecordAction(UserMetricsAction("ShowExtensions"), profile_);
  ShowSingletonTab(GURL(chrome::kChromeUIExtensionsURL));
}

void Browser::ShowBrokenPageTab(TabContents* contents) {
  UserMetrics::RecordAction(UserMetricsAction("ReportBug"), profile_);
  string16 page_title = contents->GetTitle();
  NavigationEntry* entry = contents->controller().GetActiveEntry();
  if (!entry)
    return;
  std::string page_url = entry->url().spec();
  std::vector<std::string> subst;
  subst.push_back(UTF16ToASCII(page_title));
  subst.push_back(page_url);
  std::string report_page_url =
      ReplaceStringPlaceholders(kBrokenPageUrl, subst, NULL);
  ShowSingletonTab(GURL(report_page_url));
}

void Browser::ShowOptionsTab() {
  UserMetrics::RecordAction(UserMetricsAction("ShowOptions"), profile_);
  ShowSingletonTab(GURL(chrome::kChromeUIOptionsURL));
}

void Browser::OpenClearBrowsingDataDialog() {
  UserMetrics::RecordAction(UserMetricsAction("ClearBrowsingData_ShowDlg"),
                            profile_);
  window_->ShowClearBrowsingDataDialog();
}

void Browser::OpenOptionsDialog() {
  if (CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kEnableTabbedOptions)) {
    ShowOptionsTab();
  } else {
    UserMetrics::RecordAction(UserMetricsAction("ShowOptions"), profile_);
    ShowOptionsWindow(OPTIONS_PAGE_DEFAULT, OPTIONS_GROUP_NONE, profile_);
  }
}

void Browser::OpenKeywordEditor() {
  UserMetrics::RecordAction(UserMetricsAction("EditSearchEngines"), profile_);
  window_->ShowSearchEnginesDialog();
}

void Browser::OpenPasswordManager() {
  window_->ShowPasswordManager();
}

void Browser::OpenImportSettingsDialog() {
  UserMetrics::RecordAction(UserMetricsAction("Import_ShowDlg"), profile_);
  window_->ShowImportDialog();
}

void Browser::OpenSyncMyBookmarksDialog() {
  sync_ui_util::OpenSyncMyBookmarksDialog(
      profile_, ProfileSyncService::START_FROM_WRENCH);
}

void Browser::OpenAboutChromeDialog() {
  UserMetrics::RecordAction(UserMetricsAction("AboutChrome"), profile_);
  window_->ShowAboutChromeDialog();
}

void Browser::OpenUpdateChromeDialog() {
  UserMetrics::RecordAction(UserMetricsAction("UpdateChrome"), profile_);
  window_->ShowUpdateChromeDialog();
}

void Browser::OpenHelpTab() {
  GURL help_url = google_util::AppendGoogleLocaleParam(GURL(kHelpContentUrl));
  AddTabWithURL(help_url, GURL(), PageTransition::AUTO_BOOKMARK, -1,
                TabStripModel::ADD_SELECTED, NULL, std::string());
}

void Browser::OpenThemeGalleryTabAndActivate() {
  OpenURL(GURL(l10n_util::GetStringUTF8(IDS_THEMES_GALLERY_URL)),
          GURL(), NEW_FOREGROUND_TAB, PageTransition::LINK);
  window_->Activate();
}

void Browser::OpenPrivacyDashboardTabAndActivate() {
  OpenURL(GURL(l10n_util::GetStringUTF8(IDS_PRIVACY_DASHBOARD_URL)),
          GURL(), NEW_FOREGROUND_TAB, PageTransition::LINK);
  window_->Activate();
}

void Browser::OpenAutoFillHelpTabAndActivate() {
  OpenURL(GURL(l10n_util::GetStringUTF8(IDS_AUTOFILL_HELP_URL)),
          GURL(), NEW_FOREGROUND_TAB, PageTransition::LINK);
  window_->Activate();
}

#if defined(OS_CHROMEOS)
void Browser::OpenSystemOptionsDialog() {
  UserMetrics::RecordAction(UserMetricsAction("OpenSystemOptionsDialog"),
                            profile_);
  ShowOptionsWindow(OPTIONS_PAGE_SYSTEM, OPTIONS_GROUP_NONE, profile_);
}

void Browser::OpenInternetOptionsDialog() {
  UserMetrics::RecordAction(UserMetricsAction("OpenInternetOptionsDialog"),
                            profile_);
  ShowOptionsWindow(OPTIONS_PAGE_INTERNET, OPTIONS_GROUP_NONE, profile_);
}
#endif

///////////////////////////////////////////////////////////////////////////////

// static
void Browser::SetNewHomePagePrefs(PrefService* prefs) {
  const PrefService::Preference* home_page_pref =
      prefs->FindPreference(prefs::kHomePage);
  if (home_page_pref &&
      !home_page_pref->IsManaged() &&
      !prefs->HasPrefPath(prefs::kHomePage)) {
    prefs->SetString(prefs::kHomePage,
        GoogleURLTracker::kDefaultGoogleHomepage);
  }
  const PrefService::Preference* home_page_is_new_tab_page_pref =
      prefs->FindPreference(prefs::kHomePageIsNewTabPage);
  if (home_page_is_new_tab_page_pref &&
      !home_page_is_new_tab_page_pref->IsManaged() &&
      !prefs->HasPrefPath(prefs::kHomePageIsNewTabPage))
    prefs->SetBoolean(prefs::kHomePageIsNewTabPage, false);
}

// static
void Browser::RegisterPrefs(PrefService* prefs) {
  prefs->RegisterDictionaryPref(prefs::kBrowserWindowPlacement);
  prefs->RegisterIntegerPref(prefs::kOptionsWindowLastTabIndex, 0);
  prefs->RegisterIntegerPref(prefs::kDevToolsSplitLocation, -1);
  prefs->RegisterDictionaryPref(prefs::kPreferencesWindowPlacement);
}

// static
void Browser::RegisterUserPrefs(PrefService* prefs) {
  prefs->RegisterStringPref(prefs::kHomePage,
                            chrome::kChromeUINewTabURL);
  prefs->RegisterBooleanPref(prefs::kHomePageIsNewTabPage, true);
  prefs->RegisterBooleanPref(prefs::kClearSiteDataOnExit, false);
  prefs->RegisterBooleanPref(prefs::kShowHomeButton, false);
#if defined(OS_MACOSX)
  // This really belongs in platform code, but there's no good place to
  // initialize it between the time when the AppController is created
  // (where there's no profile) and the time the controller gets another
  // crack at the start of the main event loop. By that time, BrowserInit
  // has already created the browser window, and it's too late: we need the
  // pref to be already initialized. Doing it here also saves us from having
  // to hard-code pref registration in the several unit tests that use
  // this preference.
  prefs->RegisterBooleanPref(prefs::kShowPageOptionsButtons, false);
  prefs->RegisterBooleanPref(prefs::kShowUpdatePromotionInfoBar, true);
#endif
  prefs->RegisterStringPref(prefs::kRecentlySelectedEncoding, "");
  prefs->RegisterBooleanPref(prefs::kDeleteBrowsingHistory, true);
  prefs->RegisterBooleanPref(prefs::kDeleteDownloadHistory, true);
  prefs->RegisterBooleanPref(prefs::kDeleteCache, true);
  prefs->RegisterBooleanPref(prefs::kDeleteCookies, true);
  prefs->RegisterBooleanPref(prefs::kDeletePasswords, false);
  prefs->RegisterBooleanPref(prefs::kDeleteFormData, true);
  prefs->RegisterIntegerPref(prefs::kDeleteTimePeriod, 0);
  prefs->RegisterBooleanPref(prefs::kCheckDefaultBrowser, true);
  prefs->RegisterBooleanPref(prefs::kShowOmniboxSearchHint, true);
  prefs->RegisterBooleanPref(prefs::kShowExtensionShelf, true);
  prefs->RegisterBooleanPref(prefs::kWebAppCreateOnDesktop, true);
  prefs->RegisterBooleanPref(prefs::kWebAppCreateInAppsMenu, true);
  prefs->RegisterBooleanPref(prefs::kWebAppCreateInQuickLaunchBar, true);
  prefs->RegisterBooleanPref(prefs::kUseVerticalTabs, false);
  prefs->RegisterBooleanPref(prefs::kEnableTranslate, true);
  prefs->RegisterIntegerPref(prefs::kNTPPromoViewsRemaining, 5);
}

// static
Browser* Browser::GetBrowserForController(
    const NavigationController* controller, int* index_result) {
  BrowserList::const_iterator it;
  for (it = BrowserList::begin(); it != BrowserList::end(); ++it) {
    int index = (*it)->tabstrip_model_.GetIndexOfController(controller);
    if (index != TabStripModel::kNoTab) {
      if (index_result)
        *index_result = index;
      return *it;
    }
  }

  return NULL;
}

void Browser::ExecuteCommandWithDisposition(
  int id, WindowOpenDisposition disposition) {
  // No commands are enabled if there is not yet any selected tab.
  // TODO(pkasting): It seems like we should not need this, because either
  // most/all commands should not have been enabled yet anyway or the ones that
  // are enabled should be global, or safe themselves against having no selected
  // tab.  However, Ben says he tried removing this before and got lots of
  // crashes, e.g. from Windows sending WM_COMMANDs at random times during
  // window construction.  This probably could use closer examination someday.
  if (!GetSelectedTabContents())
    return;

  DCHECK(command_updater_.IsCommandEnabled(id)) << "Invalid/disabled command";

  // If command execution is blocked then just record the command and return.
  if (block_command_execution_) {
    // We actually only allow no more than one blocked command, otherwise some
    // commands maybe lost.
    DCHECK_EQ(last_blocked_command_id_, -1);
    last_blocked_command_id_ = id;
    last_blocked_command_disposition_ = disposition;
    return;
  }

  // The order of commands in this switch statement must match the function
  // declaration order in browser.h!
  switch (id) {
    // Navigation commands
    case IDC_BACK:                  GoBack(disposition);              break;
    case IDC_FORWARD:               GoForward(disposition);           break;
    case IDC_RELOAD:                Reload(disposition);              break;
    case IDC_RELOAD_IGNORING_CACHE: ReloadIgnoringCache(disposition); break;
    case IDC_HOME:                  Home(disposition);                break;
    case IDC_OPEN_CURRENT_URL:      OpenCurrentURL();                 break;
    case IDC_STOP:                  Stop();                           break;

     // Window management commands
    case IDC_NEW_WINDOW:            NewWindow();                      break;
    case IDC_NEW_INCOGNITO_WINDOW:  NewIncognitoWindow();             break;
    case IDC_CLOSE_WINDOW:          CloseWindow();                    break;
    case IDC_NEW_TAB:               NewTab();                         break;
    case IDC_CLOSE_TAB:             CloseTab();                       break;
    case IDC_SELECT_NEXT_TAB:       SelectNextTab();                  break;
    case IDC_SELECT_PREVIOUS_TAB:   SelectPreviousTab();              break;
    case IDC_MOVE_TAB_NEXT:         MoveTabNext();                    break;
    case IDC_MOVE_TAB_PREVIOUS:     MoveTabPrevious();                break;
    case IDC_SELECT_TAB_0:
    case IDC_SELECT_TAB_1:
    case IDC_SELECT_TAB_2:
    case IDC_SELECT_TAB_3:
    case IDC_SELECT_TAB_4:
    case IDC_SELECT_TAB_5:
    case IDC_SELECT_TAB_6:
    case IDC_SELECT_TAB_7:          SelectNumberedTab(id - IDC_SELECT_TAB_0);
                                                                      break;
    case IDC_SELECT_LAST_TAB:       SelectLastTab();                  break;
    case IDC_DUPLICATE_TAB:         DuplicateTab();                   break;
    case IDC_RESTORE_TAB:           RestoreTab();                     break;
    case IDC_COPY_URL:              WriteCurrentURLToClipboard();     break;
    case IDC_SHOW_AS_TAB:           ConvertPopupToTabbedBrowser();    break;
    case IDC_FULLSCREEN:            ToggleFullscreenMode();           break;
    case IDC_EXIT:                  Exit();                           break;
    case IDC_TOGGLE_VERTICAL_TABS:  ToggleUseVerticalTabs();          break;
#if defined(OS_CHROMEOS)
    case IDC_SEARCH:                Search();                         break;
#endif

    // Page-related commands
    case IDC_SAVE_PAGE:             SavePage();                       break;
    case IDC_BOOKMARK_PAGE:         BookmarkCurrentPage();            break;
    case IDC_BOOKMARK_ALL_TABS:     BookmarkAllTabs();                break;
    case IDC_VIEW_SOURCE:           ViewSource();                     break;
    case IDC_EMAIL_PAGE_LOCATION:   EmailPageLocation();              break;
    case IDC_PRINT:                 Print();                          break;
    case IDC_ENCODING_AUTO_DETECT:  ToggleEncodingAutoDetect();       break;
    case IDC_ENCODING_UTF8:
    case IDC_ENCODING_UTF16LE:
    case IDC_ENCODING_ISO88591:
    case IDC_ENCODING_WINDOWS1252:
    case IDC_ENCODING_GBK:
    case IDC_ENCODING_GB18030:
    case IDC_ENCODING_BIG5HKSCS:
    case IDC_ENCODING_BIG5:
    case IDC_ENCODING_KOREAN:
    case IDC_ENCODING_SHIFTJIS:
    case IDC_ENCODING_ISO2022JP:
    case IDC_ENCODING_EUCJP:
    case IDC_ENCODING_THAI:
    case IDC_ENCODING_ISO885915:
    case IDC_ENCODING_MACINTOSH:
    case IDC_ENCODING_ISO88592:
    case IDC_ENCODING_WINDOWS1250:
    case IDC_ENCODING_ISO88595:
    case IDC_ENCODING_WINDOWS1251:
    case IDC_ENCODING_KOI8R:
    case IDC_ENCODING_KOI8U:
    case IDC_ENCODING_ISO88597:
    case IDC_ENCODING_WINDOWS1253:
    case IDC_ENCODING_ISO88594:
    case IDC_ENCODING_ISO885913:
    case IDC_ENCODING_WINDOWS1257:
    case IDC_ENCODING_ISO88593:
    case IDC_ENCODING_ISO885910:
    case IDC_ENCODING_ISO885914:
    case IDC_ENCODING_ISO885916:
    case IDC_ENCODING_WINDOWS1254:
    case IDC_ENCODING_ISO88596:
    case IDC_ENCODING_WINDOWS1256:
    case IDC_ENCODING_ISO88598:
    case IDC_ENCODING_ISO88598I:
    case IDC_ENCODING_WINDOWS1255:
    case IDC_ENCODING_WINDOWS1258:  OverrideEncoding(id);             break;

    // Clipboard commands
    case IDC_CUT:                   Cut();                            break;
    case IDC_COPY:                  Copy();                           break;
    case IDC_PASTE:                 Paste();                          break;

    // Find-in-page
    case IDC_FIND:                  Find();                           break;
    case IDC_FIND_NEXT:             FindNext();                       break;
    case IDC_FIND_PREVIOUS:         FindPrevious();                   break;

    // Zoom
    case IDC_ZOOM_PLUS:             Zoom(PageZoom::ZOOM_IN);          break;
    case IDC_ZOOM_NORMAL:           Zoom(PageZoom::RESET);            break;
    case IDC_ZOOM_MINUS:            Zoom(PageZoom::ZOOM_OUT);         break;

    // Focus various bits of UI
    case IDC_FOCUS_TOOLBAR:         FocusToolbar();                   break;
    case IDC_FOCUS_LOCATION:        FocusLocationBar();               break;
    case IDC_FOCUS_SEARCH:          FocusSearch();                    break;
    case IDC_FOCUS_MENU_BAR:        FocusAppMenu();                   break;
    case IDC_FOCUS_BOOKMARKS:       FocusBookmarksToolbar();          break;
    case IDC_FOCUS_CHROMEOS_STATUS: FocusChromeOSStatus();            break;
    case IDC_FOCUS_NEXT_PANE:       FocusNextPane();                  break;
    case IDC_FOCUS_PREVIOUS_PANE:   FocusPreviousPane();              break;

    // Show various bits of UI
    case IDC_OPEN_FILE:             OpenFile();                       break;
    case IDC_CREATE_SHORTCUTS:      OpenCreateShortcutsDialog();      break;
    case IDC_DEV_TOOLS:             ToggleDevToolsWindow(
                                        DEVTOOLS_TOGGLE_ACTION_NONE);
                                    break;
    case IDC_DEV_TOOLS_CONSOLE:     ToggleDevToolsWindow(
                                        DEVTOOLS_TOGGLE_ACTION_SHOW_CONSOLE);
                                    break;
    case IDC_DEV_TOOLS_INSPECT:     ToggleDevToolsWindow(
                                        DEVTOOLS_TOGGLE_ACTION_INSPECT);
                                    break;
    case IDC_TASK_MANAGER:          OpenTaskManager();                break;
    case IDC_REPORT_BUG:            OpenBugReportDialog();            break;

    case IDC_SHOW_BOOKMARK_BAR:     ToggleBookmarkBar();              break;
    case IDC_SHOW_EXTENSION_SHELF:  ToggleExtensionShelf();           break;

    case IDC_SHOW_BOOKMARK_MANAGER: OpenBookmarkManager();            break;
    case IDC_SHOW_APP_MENU:         ShowAppMenu();                    break;
    case IDC_SHOW_HISTORY:          ShowHistoryTab();                 break;
    case IDC_SHOW_DOWNLOADS:        ShowDownloadsTab();               break;
    case IDC_MANAGE_EXTENSIONS:     ShowExtensionsTab();              break;
    case IDC_SYNC_BOOKMARKS:        OpenSyncMyBookmarksDialog();      break;
    case IDC_OPTIONS:               OpenOptionsDialog();              break;
    case IDC_EDIT_SEARCH_ENGINES:   OpenKeywordEditor();              break;
    case IDC_VIEW_PASSWORDS:        OpenPasswordManager();            break;
    case IDC_CLEAR_BROWSING_DATA:   OpenClearBrowsingDataDialog();    break;
    case IDC_IMPORT_SETTINGS:       OpenImportSettingsDialog();       break;
    case IDC_ABOUT:
      if (Singleton<UpgradeDetector>::get()->notify_upgrade())
        OpenUpdateChromeDialog();
      else
        OpenAboutChromeDialog();
      break;
    case IDC_HELP_PAGE:             OpenHelpTab();                    break;
#if defined(OS_CHROMEOS)
    case IDC_SYSTEM_OPTIONS:        OpenSystemOptionsDialog();        break;
    case IDC_INTERNET_OPTIONS:      OpenInternetOptionsDialog();      break;
#endif

    default:
      LOG(WARNING) << "Received Unimplemented Command: " << id;
      break;
  }
}

bool Browser::IsReservedCommand(int command_id) {
  return command_id == IDC_CLOSE_TAB ||
         command_id == IDC_CLOSE_WINDOW ||
         command_id == IDC_NEW_INCOGNITO_WINDOW ||
         command_id == IDC_NEW_TAB ||
         command_id == IDC_NEW_WINDOW ||
         command_id == IDC_RESTORE_TAB ||
         command_id == IDC_SELECT_NEXT_TAB ||
         command_id == IDC_SELECT_PREVIOUS_TAB ||
         command_id == IDC_EXIT ||
         command_id == IDC_SEARCH;
}

void Browser::SetBlockCommandExecution(bool block) {
  block_command_execution_ = block;
  if (block) {
    last_blocked_command_id_ = -1;
    last_blocked_command_disposition_ = CURRENT_TAB;
  }
}

int Browser::GetLastBlockedCommand(WindowOpenDisposition* disposition) {
  if (disposition)
    *disposition = last_blocked_command_disposition_;
  return last_blocked_command_id_;
}

///////////////////////////////////////////////////////////////////////////////
// Browser, CommandUpdater::CommandUpdaterDelegate implementation:

void Browser::ExecuteCommand(int id) {
  ExecuteCommandWithDisposition(id, CURRENT_TAB);
}

///////////////////////////////////////////////////////////////////////////////
// Browser, TabStripModelDelegate implementation:

TabContents* Browser::AddBlankTab(bool foreground) {
  return AddBlankTabAt(-1, foreground);
}

TabContents* Browser::AddBlankTabAt(int index, bool foreground) {
  // Time new tab page creation time.  We keep track of the timing data in
  // TabContents, but we want to include the time it takes to create the
  // TabContents object too.
  base::TimeTicks new_tab_start_time = base::TimeTicks::Now();
  TabContents* tab_contents = AddTabWithURL(
      GURL(chrome::kChromeUINewTabURL), GURL(), PageTransition::TYPED, index,
      foreground ? TabStripModel::ADD_SELECTED : TabStripModel::ADD_NONE, NULL,
      std::string());
  tab_contents->set_new_tab_start_time(new_tab_start_time);
  return tab_contents;
}

Browser* Browser::CreateNewStripWithContents(TabContents* detached_contents,
                                             const gfx::Rect& window_bounds,
                                             const DockInfo& dock_info) {
  DCHECK(CanSupportWindowFeature(FEATURE_TABSTRIP));

  gfx::Rect new_window_bounds = window_bounds;
  bool maximize = false;
  if (dock_info.GetNewWindowBounds(&new_window_bounds, &maximize))
    dock_info.AdjustOtherWindowBounds();

  // Create an empty new browser window the same size as the old one.
  Browser* browser = new Browser(TYPE_NORMAL, profile_);
  browser->set_override_bounds(new_window_bounds);
  browser->set_maximized_state(
      maximize ? MAXIMIZED_STATE_MAXIMIZED : MAXIMIZED_STATE_UNMAXIMIZED);
  browser->CreateBrowserWindow();
  browser->tabstrip_model()->AppendTabContents(detached_contents, true);
  // Make sure the loading state is updated correctly, otherwise the throbber
  // won't start if the page is loading.
  browser->LoadingStateChanged(detached_contents);
  return browser;
}

void Browser::ContinueDraggingDetachedTab(TabContents* contents,
                                          const gfx::Rect& window_bounds,
                                          const gfx::Rect& tab_bounds) {
  Browser* browser = new Browser(TYPE_NORMAL, profile_);
  browser->set_override_bounds(window_bounds);
  browser->CreateBrowserWindow();
  browser->tabstrip_model()->AppendTabContents(contents, true);
  browser->LoadingStateChanged(contents);
  browser->window()->Show();
  browser->window()->ContinueDraggingDetachedTab(tab_bounds);
}

int Browser::GetDragActions() const {
  return TAB_TEAROFF_ACTION | (tab_count() > 1 ? TAB_MOVE_ACTION : 0);
}

TabContents* Browser::CreateTabContentsForURL(
    const GURL& url, const GURL& referrer, Profile* profile,
    PageTransition::Type transition, bool defer_load,
    SiteInstance* instance) const {
  TabContents* contents = new TabContents(profile, instance,
      MSG_ROUTING_NONE, tabstrip_model_.GetSelectedTabContents());

  if (!defer_load) {
    // Load the initial URL before adding the new tab contents to the tab strip
    // so that the tab contents has navigation state.
    contents->controller().LoadURL(url, referrer, transition);
  }

  return contents;
}

bool Browser::CanDuplicateContentsAt(int index) {
  NavigationController& nc = GetTabContentsAt(index)->controller();
  return nc.tab_contents() && nc.GetLastCommittedEntry();
}

void Browser::DuplicateContentsAt(int index) {
  TabContents* contents = GetTabContentsAt(index);
  TabContents* new_contents = NULL;
  DCHECK(contents);
  bool pinned = false;

  if (CanSupportWindowFeature(FEATURE_TABSTRIP)) {
    // If this is a tabbed browser, just create a duplicate tab inside the same
    // window next to the tab being duplicated.
    new_contents = contents->Clone();
    pinned = tabstrip_model_.IsTabPinned(index);
    int add_types = TabStripModel::ADD_SELECTED |
        TabStripModel::ADD_INHERIT_GROUP |
        (pinned ? TabStripModel::ADD_PINNED : 0);
    tabstrip_model_.InsertTabContentsAt(index + 1, new_contents, add_types);
  } else {
    Browser* browser = NULL;
    if (type_ & TYPE_APP) {
      DCHECK((type_ & TYPE_POPUP) == 0);
      DCHECK(type_ != TYPE_APP_PANEL);
      browser = Browser::CreateForApp(app_name_, extension_app_, profile_,
                                      false);
    } else if (type_ == TYPE_POPUP) {
      browser = Browser::CreateForPopup(profile_);
    }

    // Preserve the size of the original window. The new window has already
    // been given an offset by the OS, so we shouldn't copy the old bounds.
    BrowserWindow* new_window = browser->window();
    new_window->SetBounds(gfx::Rect(new_window->GetRestoredBounds().origin(),
                          window()->GetRestoredBounds().size()));

    // We need to show the browser now. Otherwise ContainerWin assumes the
    // TabContents is invisible and won't size it.
    browser->window()->Show();

    // The page transition below is only for the purpose of inserting the tab.
    new_contents = browser->AddTab(
        contents->Clone()->controller().tab_contents(),
        PageTransition::LINK);
  }

  if (profile_->HasSessionService()) {
    SessionService* session_service = profile_->GetSessionService();
    if (session_service)
      session_service->TabRestored(&new_contents->controller(), pinned);
  }
}

void Browser::CloseFrameAfterDragSession() {
#if defined(OS_WIN) || defined(OS_LINUX)
  // This is scheduled to run after we return to the message loop because
  // otherwise the frame will think the drag session is still active and ignore
  // the request.
  // TODO(port): figure out what is required here in a cross-platform world
  MessageLoop::current()->PostTask(
      FROM_HERE, method_factory_.NewRunnableMethod(&Browser::CloseFrame));
#endif
}

void Browser::CreateHistoricalTab(TabContents* contents) {
  // We don't create historical tabs for incognito windows or windows without
  // profiles.
  if (!profile() || profile()->IsOffTheRecord() ||
      !profile()->GetTabRestoreService()) {
    return;
  }

  // We only create historical tab entries for tabbed browser windows.
  if (CanSupportWindowFeature(FEATURE_TABSTRIP)) {
    profile()->GetTabRestoreService()->CreateHistoricalTab(
        &contents->controller());
  }
}

bool Browser::RunUnloadListenerBeforeClosing(TabContents* contents) {
  return Browser::RunUnloadEventsHelper(contents);
}

bool Browser::CanReloadContents(TabContents* source) const {
  return type() != TYPE_DEVTOOLS;
}

bool Browser::CanCloseContentsAt(int index) {
  if (!CanCloseTab())
    return false;
  if (tabstrip_model_.count() > 1)
    return true;
  // We are closing the last tab for this browser. Make sure to check for
  // in-progress downloads.
  // Note that the next call when it returns false will ask the user for
  // confirmation before closing the browser if the user decides so.
  return CanCloseWithInProgressDownloads();
}

bool Browser::CanBookmarkAllTabs() const {
  BookmarkModel* model = profile()->GetBookmarkModel();
  return (model && model->IsLoaded() && (tab_count() > 1));
}

void Browser::BookmarkAllTabs() {
  BookmarkModel* model = profile()->GetBookmarkModel();
  DCHECK(model && model->IsLoaded());

  BookmarkEditor::EditDetails details;
  details.type = BookmarkEditor::EditDetails::NEW_FOLDER;
  bookmark_utils::GetURLsForOpenTabs(this, &(details.urls));
  DCHECK(!details.urls.empty());

  BookmarkEditor::Show(window()->GetNativeHandle(), profile_,
                       model->GetParentForNewNodes(),  details,
                       BookmarkEditor::SHOW_TREE);
}

bool Browser::CanCloseTab() const {
  TabCloseableStateWatcher* watcher =
      g_browser_process->tab_closeable_state_watcher();
  return !watcher || watcher->CanCloseTab(this);
}

void Browser::ToggleUseVerticalTabs() {
  use_vertical_tabs_.SetValue(!UseVerticalTabs());
  UseVerticalTabsChanged();
}

///////////////////////////////////////////////////////////////////////////////
// Browser, TabStripModelObserver implementation:

void Browser::TabInsertedAt(TabContents* contents,
                            int index,
                            bool foreground) {
  contents->set_delegate(this);
  contents->controller().SetWindowID(session_id());

  SyncHistoryWithTabs(index);

  // Make sure the loading state is updated correctly, otherwise the throbber
  // won't start if the page is loading.
  LoadingStateChanged(contents);

  // If the tab crashes in the beforeunload or unload handler, it won't be
  // able to ack. But we know we can close it.
  registrar_.Add(this, NotificationType::TAB_CONTENTS_DISCONNECTED,
                 Source<TabContents>(contents));
}

void Browser::TabClosingAt(TabContents* contents, int index) {
  NotificationService::current()->Notify(
      NotificationType::TAB_CLOSING,
      Source<NavigationController>(&contents->controller()),
      NotificationService::NoDetails());

  // Sever the TabContents' connection back to us.
  contents->set_delegate(NULL);
}

void Browser::TabDetachedAt(TabContents* contents, int index) {
  TabDetachedAtImpl(contents, index, DETACH_TYPE_DETACH);
}

void Browser::TabDeselectedAt(TabContents* contents, int index) {
  // Save what the user's currently typing, so it can be restored when we
  // switch back to this tab.
  window_->GetLocationBar()->SaveStateToContents(contents);
}

void Browser::TabSelectedAt(TabContents* old_contents,
                            TabContents* new_contents,
                            int index,
                            bool user_gesture) {
  DCHECK(old_contents != new_contents);

  // If we have any update pending, do it now.
  if (!chrome_updater_factory_.empty() && old_contents)
    ProcessPendingUIUpdates();

  // Propagate the profile to the location bar.
  UpdateToolbar(true);

  // Update reload/stop state.
  UpdateReloadStopState(new_contents->is_loading(), true);

  // Update commands to reflect current state.
  UpdateCommandsForTabState();

  // Reset the status bubble.
  StatusBubble* status_bubble = GetStatusBubble();
  if (status_bubble) {
    status_bubble->Hide();

    // Show the loading state (if any).
    status_bubble->SetStatus(GetSelectedTabContents()->GetStatusText());
  }

  if (HasFindBarController()) {
    find_bar_controller_->ChangeTabContents(new_contents);
    find_bar_controller_->find_bar()->MoveWindowIfNecessary(gfx::Rect(), true);
  }

  // Update sessions. Don't force creation of sessions. If sessions doesn't
  // exist, the change will be picked up by sessions when created.
  if (profile_->HasSessionService()) {
    SessionService* session_service = profile_->GetSessionService();
    if (session_service && !tabstrip_model_.closing_all()) {
      session_service->SetSelectedTabInWindow(
          session_id(), tabstrip_model_.selected_index());
    }
  }
}

void Browser::TabMoved(TabContents* contents,
                       int from_index,
                       int to_index) {
  DCHECK(from_index >= 0 && to_index >= 0);
  // Notify the history service.
  SyncHistoryWithTabs(std::min(from_index, to_index));
}

void Browser::TabReplacedAt(TabContents* old_contents,
                            TabContents* new_contents, int index) {
  TabDetachedAtImpl(old_contents, index, DETACH_TYPE_REPLACE);
  TabInsertedAt(new_contents, index,
                (index == tabstrip_model_.selected_index()));

  int entry_count = new_contents->controller().entry_count();
  if (entry_count > 0) {
    // Send out notification so that observers are updated appropriately.
    new_contents->controller().NotifyEntryChanged(
        new_contents->controller().GetEntryAtIndex(entry_count - 1),
        entry_count - 1);
  }
}

void Browser::TabPinnedStateChanged(TabContents* contents, int index) {
  if (!profile()->HasSessionService())
    return;
  SessionService* session_service = profile()->GetSessionService();
  if (session_service) {
    session_service->SetPinnedState(
        session_id(),
        GetTabContentsAt(index)->controller().session_id(),
        tabstrip_model_.IsTabPinned(index));
  }
}

void Browser::TabStripEmpty() {
  // Close the frame after we return to the message loop (not immediately,
  // otherwise it will destroy this object before the stack has a chance to
  // cleanly unwind.)
  // Note: This will be called several times if TabStripEmpty is called several
  //       times. This is because it does not close the window if tabs are
  //       still present.
  // NOTE: If you change to be immediate (no invokeLater) then you'll need to
  //       update BrowserList::CloseAllBrowsers.
  MessageLoop::current()->PostTask(
      FROM_HERE, method_factory_.NewRunnableMethod(&Browser::CloseFrame));
}

///////////////////////////////////////////////////////////////////////////////
// Browser, PageNavigator implementation:
void Browser::OpenURL(const GURL& url, const GURL& referrer,
                      WindowOpenDisposition disposition,
                      PageTransition::Type transition) {
  OpenURLFromTab(NULL, url, referrer, disposition, transition);
}

///////////////////////////////////////////////////////////////////////////////
// Browser, TabContentsDelegate implementation:

void Browser::OpenURLFromTab(TabContents* source,
                             const GURL& url,
                             const GURL& referrer,
                             WindowOpenDisposition disposition,
                             PageTransition::Type transition) {
  OpenURLAtIndex(source, url, referrer, disposition, transition, -1,
                 TabStripModel::ADD_NONE);
}

void Browser::NavigationStateChanged(const TabContents* source,
                                     unsigned changed_flags) {
  // Only update the UI when something visible has changed.
  if (changed_flags)
    ScheduleUIUpdate(source, changed_flags);

  // We don't schedule updates to commands since they will only change once per
  // navigation, so we don't have to worry about flickering.
  if (changed_flags & TabContents::INVALIDATE_URL)
    UpdateCommandsForTabState();
}

void Browser::AddNewContents(TabContents* source,
                             TabContents* new_contents,
                             WindowOpenDisposition disposition,
                             const gfx::Rect& initial_pos,
                             bool user_gesture) {
  DCHECK(disposition != SAVE_TO_DISK);  // No code for this yet
  DCHECK(disposition != CURRENT_TAB);  // Can't create a new contents for the
                                       // current tab.

  // If this is a window with no tabstrip, we can only have one tab so we need
  // to process this in tabbed browser window.
  if (!CanSupportWindowFeature(FEATURE_TABSTRIP) &&
      tabstrip_model_.count() > 0 && disposition != NEW_WINDOW &&
      disposition != NEW_POPUP) {
    Browser* b = GetOrCreateTabbedBrowser(profile_);
    DCHECK(b);
    PageTransition::Type transition = PageTransition::LINK;
    // If we were called from an "installed webapp" we want to emulate the code
    // that is run from browser_init.cc for links from external applications.
    // This means we need to open the tab with the START PAGE transition.
    // AddNewContents doesn't support this but the TabStripModel's
    // AddTabContents method does.
    if (type_ & TYPE_APP)
      transition = PageTransition::START_PAGE;
    b->tabstrip_model()->AddTabContents(
        new_contents, -1, transition, TabStripModel::ADD_SELECTED);
    b->window()->Show();
    return;
  }

  if (disposition == NEW_POPUP) {
    BuildPopupWindow(source, new_contents, initial_pos);
  } else if (disposition == NEW_WINDOW) {
    Browser* browser = Browser::Create(profile_);
    browser->AddNewContents(source, new_contents, NEW_FOREGROUND_TAB,
                            initial_pos, user_gesture);
    browser->window()->Show();
  } else if (disposition != SUPPRESS_OPEN) {
    tabstrip_model_.AddTabContents(
        new_contents, -1, PageTransition::LINK,
        disposition == NEW_FOREGROUND_TAB ? TabStripModel::ADD_SELECTED :
                                            TabStripModel::ADD_NONE);
  }
}

void Browser::ActivateContents(TabContents* contents) {
  tabstrip_model_.SelectTabContentsAt(
      tabstrip_model_.GetIndexOfTabContents(contents), false);
  window_->Activate();
}

void Browser::LoadingStateChanged(TabContents* source) {
  window_->UpdateLoadingAnimations(tabstrip_model_.TabsAreLoading());
  window_->UpdateTitleBar();

  if (source == GetSelectedTabContents()) {
    UpdateReloadStopState(source->is_loading(), false);
    if (GetStatusBubble())
      GetStatusBubble()->SetStatus(GetSelectedTabContents()->GetStatusText());

    if (!source->is_loading() &&
        pending_web_app_action_ == UPDATE_SHORTCUT) {
      // Schedule a shortcut update when web application info is available if
      // last committed entry is not NULL. Last committed entry could be NULL
      // when an interstitial page is injected (e.g. bad https certificate,
      // malware site etc). When this happens, we abort the shortcut update.
      NavigationEntry* entry = source->controller().GetLastCommittedEntry();
      if (entry) {
        source->render_view_host()->GetApplicationInfo(entry->page_id());
      } else {
        pending_web_app_action_ = NONE;
      }
    }
  }
}

void Browser::CloseContents(TabContents* source) {
  if (is_attempting_to_close_browser_) {
    // If we're trying to close the browser, just clear the state related to
    // waiting for unload to fire. Don't actually try to close the tab as it
    // will go down the slow shutdown path instead of the fast path of killing
    // all the renderer processes.
    ClearUnloadState(source);
    return;
  }

  int index = tabstrip_model_.GetIndexOfTabContents(source);
  if (index == TabStripModel::kNoTab) {
    NOTREACHED() << "CloseContents called for tab not in our strip";
    return;
  }
  tabstrip_model_.CloseTabContentsAt(
      index,
      TabStripModel::CLOSE_CREATE_HISTORICAL_TAB);
}

void Browser::MoveContents(TabContents* source, const gfx::Rect& pos) {
  if ((type() & TYPE_POPUP) == 0) {
    NOTREACHED() << "moving invalid browser type";
    return;
  }
  window_->SetBounds(pos);
}

void Browser::DetachContents(TabContents* source) {
  int index = tabstrip_model_.GetIndexOfTabContents(source);
  if (index >= 0)
    tabstrip_model_.DetachTabContentsAt(index);
}

bool Browser::IsPopup(TabContents* source) {
  // A non-tabbed BROWSER is an unconstrained popup.
  return !!(type() & TYPE_POPUP);
}

void Browser::ToolbarSizeChanged(TabContents* source, bool is_animating) {
  if (source == GetSelectedTabContents() || source == NULL) {
    // This will refresh the shelf if needed.
    window_->SelectedTabToolbarSizeChanged(is_animating);
  }
}

void Browser::ExtensionShelfSizeChanged() {
  window_->SelectedTabExtensionShelfSizeChanged();
}

void Browser::URLStarredChanged(TabContents* source, bool starred) {
  if (source == GetSelectedTabContents())
    window_->SetStarredState(starred);
}

void Browser::ContentsMouseEvent(
    TabContents* source, const gfx::Point& location, bool motion) {
  if (!GetStatusBubble())
    return;

  if (source == GetSelectedTabContents()) {
    GetStatusBubble()->MouseMoved(location, !motion);
    if (!motion)
      GetStatusBubble()->SetURL(GURL(), std::wstring());
  }
}

void Browser::UpdateTargetURL(TabContents* source, const GURL& url) {
  if (!GetStatusBubble())
    return;

  if (source == GetSelectedTabContents()) {
    PrefService* prefs = profile_->GetPrefs();
    GetStatusBubble()->SetURL(
        url, UTF8ToWide(prefs->GetString(prefs::kAcceptLanguages)));
  }
}

void Browser::UpdateDownloadShelfVisibility(bool visible) {
  if (GetStatusBubble())
    GetStatusBubble()->UpdateDownloadShelfVisibility(visible);
}

bool Browser::UseVerticalTabs() const {
  return use_vertical_tabs_.GetValue();
}

void Browser::ContentsZoomChange(bool zoom_in) {
  ExecuteCommand(zoom_in ? IDC_ZOOM_PLUS : IDC_ZOOM_MINUS);
}

void Browser::OnContentSettingsChange(TabContents* source) {
  if (source == GetSelectedTabContents())
    window_->GetLocationBar()->UpdateContentSettingsIcons();
}

void Browser::SetTabContentBlocked(TabContents* contents, bool blocked) {
  int index = tabstrip_model()->GetIndexOfTabContents(contents);
  if (index == TabStripModel::kNoTab) {
    NOTREACHED();
    return;
  }
  tabstrip_model()->SetTabBlocked(index, blocked);
}

void Browser::TabContentsFocused(TabContents* tab_content) {
  window_->TabContentsFocused(tab_content);
}

bool Browser::TakeFocus(bool reverse) {
  NotificationService::current()->Notify(
      NotificationType::FOCUS_RETURNED_TO_BROWSER,
      Source<Browser>(this),
      NotificationService::NoDetails());
  return false;
}

bool Browser::IsApplication() const {
  return (type_ & TYPE_APP) != 0;
}

void Browser::ConvertContentsToApplication(TabContents* contents) {
  const GURL& url = contents->controller().GetActiveEntry()->url();
  std::wstring app_name = web_app::GenerateApplicationNameFromURL(url);
  RegisterAppPrefs(app_name);

  DetachContents(contents);
  Browser* browser = Browser::CreateForApp(app_name, NULL, profile_, false);
  browser->tabstrip_model()->AppendTabContents(contents, true);
  TabContents* tab_contents = browser->GetSelectedTabContents();
  tab_contents->GetMutableRendererPrefs()->can_accept_load_drops = false;
  tab_contents->render_view_host()->SyncRendererPrefs();
  browser->window()->Show();
}

bool Browser::ShouldDisplayURLField() {
  return !IsApplication();
}

void Browser::BeforeUnloadFired(TabContents* tab,
                                bool proceed,
                                bool* proceed_to_fire_unload) {
  if (!is_attempting_to_close_browser_) {
    *proceed_to_fire_unload = proceed;
    if (!proceed)
      tab->set_closed_by_user_gesture(false);
    return;
  }

  if (!proceed) {
    CancelWindowClose();
    *proceed_to_fire_unload = false;
    tab->set_closed_by_user_gesture(false);
    return;
  }

  if (RemoveFromSet(&tabs_needing_before_unload_fired_, tab)) {
    // Now that beforeunload has fired, put the tab on the queue to fire
    // unload.
    tabs_needing_unload_fired_.insert(tab);
    ProcessPendingTabs();
    // We want to handle firing the unload event ourselves since we want to
    // fire all the beforeunload events before attempting to fire the unload
    // events should the user cancel closing the browser.
    *proceed_to_fire_unload = false;
    return;
  }

  *proceed_to_fire_unload = true;
}

gfx::Rect Browser::GetRootWindowResizerRect() const {
  return window_->GetRootWindowResizerRect();
}

void Browser::ShowHtmlDialog(HtmlDialogUIDelegate* delegate,
                             gfx::NativeWindow parent_window) {
  window_->ShowHTMLDialog(delegate, parent_window);
}

void Browser::SetFocusToLocationBar(bool select_all) {
  // Two differences between this and FocusLocationBar():
  // (1) This doesn't get recorded in user metrics, since it's called
  //     internally.
  // (2) This checks whether the location bar can be focused, and if not, clears
  //     the focus.  FocusLocationBar() is only reached when the location bar is
  //     focusable, but this may be reached at other times, e.g. while in
  //     fullscreen mode, where we need to leave focus in a consistent state.
  window_->SetFocusToLocationBar(select_all);
}

void Browser::RenderWidgetShowing() {
  window_->DisableInactiveFrame();
}

int Browser::GetExtraRenderViewHeight() const {
  return window_->GetExtraRenderViewHeight();
}

void Browser::OnStartDownload(DownloadItem* download) {
  if (!window())
    return;

#if defined(OS_CHROMEOS)
  // Don't show content browser for extension/theme downloads from gallery.
  if (download->is_extension_install() &&
      ExtensionsService::IsDownloadFromGallery(download->url(),
                                               download->referrer_url()))
    return;

  // skip the download shelf and just open the file browser in chromeos
  std::string arg = download->full_path().DirName().value();
  FileBrowseUI::OpenPopup(profile_,
                          arg,
                          FileBrowseUI::kPopupWidth,
                          FileBrowseUI::kPopupHeight);

#else
  // GetDownloadShelf creates the download shelf if it was not yet created.
  window()->GetDownloadShelf()->AddDownload(new DownloadItemModel(download));

  // Don't show the animation for "Save file" downloads.
  if (download->total_bytes() <= 0)
    return;

  // For non-theme extensions, we don't show the download animation.
  if (download->is_extension_install() &&
      !ExtensionsService::IsDownloadFromMiniGallery(download->url()))
    return;

  TabContents* current_tab = GetSelectedTabContents();
  // We make this check for the case of minimized windows, unit tests, etc.
  if (platform_util::IsVisible(current_tab->GetNativeView()) &&
      Animation::ShouldRenderRichAnimation()) {
    DownloadStartedAnimation::Show(current_tab);
  }
#endif
}

void Browser::ConfirmAddSearchProvider(const TemplateURL* template_url,
                                       Profile* profile) {
  window()->ConfirmAddSearchProvider(template_url, profile);
}

void Browser::ShowPageInfo(Profile* profile,
                           const GURL& url,
                           const NavigationEntry::SSLStatus& ssl,
                           bool show_history) {
  window()->ShowPageInfo(profile, url, ssl, show_history);
}

bool Browser::PreHandleKeyboardEvent(const NativeWebKeyboardEvent& event,
                                     bool* is_keyboard_shortcut) {
  return window()->PreHandleKeyboardEvent(event, is_keyboard_shortcut);
}

void Browser::HandleKeyboardEvent(const NativeWebKeyboardEvent& event) {
  window()->HandleKeyboardEvent(event);
}

void Browser::ShowRepostFormWarningDialog(TabContents *tab_contents) {
  window()->ShowRepostFormWarningDialog(tab_contents);
}

void Browser::ShowContentSettingsWindow(ContentSettingsType content_type) {
  window()->ShowContentSettingsWindow(content_type,
                                      profile_->GetOriginalProfile());
}

void Browser::ShowCollectedCookiesDialog(TabContents *tab_contents) {
  window()->ShowCollectedCookiesDialog(tab_contents);
}

bool Browser::ShouldAddNavigationsToHistory() const {
  // Don't update history if running as app.
  return !IsApplication();
}

void Browser::OnDidGetApplicationInfo(TabContents* tab_contents,
                                      int32 page_id) {
  TabContents* current_tab = GetSelectedTabContents();
  if (current_tab != tab_contents)
    return;

  NavigationEntry* entry = current_tab->controller().GetLastCommittedEntry();
  if (!entry || (entry->page_id() != page_id))
    return;

  switch (pending_web_app_action_) {
    case CREATE_SHORTCUT: {
      window()->ShowCreateShortcutsDialog(current_tab);
      break;
    }
    case UPDATE_SHORTCUT: {
      web_app::UpdateShortcutForTabContents(current_tab);
      break;
    }
    default:
      NOTREACHED();
      break;
  }

  pending_web_app_action_ = NONE;
}

Browser* Browser::GetBrowser() {
  return this;
}

///////////////////////////////////////////////////////////////////////////////
// Browser, SelectFileDialog::Listener implementation:

void Browser::FileSelected(const FilePath& path, int index, void* params) {
  profile_->set_last_selected_directory(path.DirName());
  GURL file_url = net::FilePathToFileURL(path);
  if (!file_url.is_empty())
    OpenURL(file_url, GURL(), CURRENT_TAB, PageTransition::TYPED);
}

///////////////////////////////////////////////////////////////////////////////
// Browser, NotificationObserver implementation:

void Browser::Observe(NotificationType type,
                      const NotificationSource& source,
                      const NotificationDetails& details) {
  switch (type.value) {
    case NotificationType::TAB_CONTENTS_DISCONNECTED:
      if (is_attempting_to_close_browser_) {
        // Need to do this asynchronously as it will close the tab, which is
        // currently on the call stack above us.
        MessageLoop::current()->PostTask(
            FROM_HERE,
            method_factory_.NewRunnableMethod(&Browser::ClearUnloadState,
                Source<TabContents>(source).ptr()));
      }
      break;

    case NotificationType::SSL_VISIBLE_STATE_CHANGED:
      // When the current tab's SSL state changes, we need to update the URL
      // bar to reflect the new state. Note that it's possible for the selected
      // tab contents to be NULL. This is because we listen for all sources
      // (NavigationControllers) for convenience, so the notification could
      // actually be for a different window while we're doing asynchronous
      // closing of this one.
      if (GetSelectedTabContents() &&
          &GetSelectedTabContents()->controller() ==
          Source<NavigationController>(source).ptr())
        UpdateToolbar(false);
      break;

    case NotificationType::EXTENSION_UPDATE_DISABLED: {
      // Show the UI if the extension was disabled for escalated permissions.
      Profile* profile = Source<Profile>(source).ptr();
      DCHECK_EQ(profile_, profile);
      ExtensionsService* service = profile->GetExtensionsService();
      DCHECK(service);
      Extension* extension = Details<Extension>(details).ptr();
      if (service->extension_prefs()->DidExtensionEscalatePermissions(
              extension->id()))
        ShowExtensionDisabledUI(service, profile_, extension);
      break;
    }

    case NotificationType::EXTENSION_UNLOADED:
    case NotificationType::EXTENSION_UNLOADED_DISABLED: {
      window()->GetLocationBar()->UpdatePageActions();

      // Close any tabs from the unloaded extension.
      Extension* extension = Details<Extension>(details).ptr();
      for (int i = 0; i < tabstrip_model_.count(); i++) {
        TabContents* tc = tabstrip_model_.GetTabContentsAt(i);
        if (tc->GetURL().SchemeIs(chrome::kExtensionScheme) &&
            tc->GetURL().host() == extension->id()) {
          CloseTabContents(tc);
          return;
        }
      }

      break;
    }

    case NotificationType::EXTENSION_PROCESS_TERMINATED: {
      window()->GetLocationBar()->InvalidatePageActions();

      TabContents* tab_contents = GetSelectedTabContents();
      if (!tab_contents)
        break;
      ExtensionsService* extensions_service =
          Source<Profile>(source).ptr()->GetExtensionsService();
      ExtensionHost* extension_host = Details<ExtensionHost>(details).ptr();
      tab_contents->AddInfoBar(new CrashedExtensionInfoBarDelegate(
          tab_contents, extensions_service, extension_host->extension()));
      break;
    }

    case NotificationType::EXTENSION_LOADED: {
      window()->GetLocationBar()->UpdatePageActions();

      // If any "This extension has crashed" InfoBarDelegates are around for
      // this extension, it means that it has been reloaded in another window
      // so just remove the remaining CrashedExtensionInfoBarDelegate objects.
      TabContents* tab_contents = GetSelectedTabContents();
      if (!tab_contents)
        break;
      Extension* extension = Details<Extension>(details).ptr();
      CrashedExtensionInfoBarDelegate* delegate = NULL;
      for (int i = 0; i < tab_contents->infobar_delegate_count();) {
        delegate = tab_contents->GetInfoBarDelegateAt(i)->
            AsCrashedExtensionInfoBarDelegate();
        if (delegate && delegate->extension_id() == extension->id()) {
          tab_contents->RemoveInfoBar(delegate);
          continue;
        }
        // Only increment |i| if we didn't remove an entry.
        ++i;
      }
      break;
    }

    case NotificationType::BROWSER_THEME_CHANGED:
      window()->UserChangedTheme();
      break;

    case NotificationType::EXTENSION_READY_FOR_INSTALL: {
      // Handle EXTENSION_READY_FOR_INSTALL for last active normal browser.
      if (BrowserList::FindBrowserWithType(profile(),
                                           Browser::TYPE_NORMAL,
                                           true) != this)
        break;

      // We only want to show the loading dialog for themes, but we don't want
      // to wait until unpack to find out an extension is a theme, so we test
      // the download_url GURL instead. This means that themes in the extensions
      // gallery won't get the loading dialog.
      GURL download_url = *(Details<GURL>(details).ptr());
      if (ExtensionsService::IsDownloadFromMiniGallery(download_url))
        window()->ShowThemeInstallBubble();
      break;
    }

    case NotificationType::PROFILE_ERROR: {
      if (BrowserList::GetLastActive() != this)
        break;
      int* message_id = Details<int>(details).ptr();
      window()->ShowProfileErrorDialog(*message_id);
      break;
    }

    case NotificationType::PREF_CHANGED: {
      if (*(Details<std::wstring>(details).ptr()) == prefs::kUseVerticalTabs)
        UseVerticalTabsChanged();
      else
        NOTREACHED();
      break;
    }

    default:
      NOTREACHED() << "Got a notification we didn't register for.";
  }
}

///////////////////////////////////////////////////////////////////////////////
// Browser, ProfileSyncServiceObserver implementation:

void Browser::OnStateChanged() {
  DCHECK(profile_->GetProfileSyncService());

#if !defined(OS_MACOSX)
  const bool show_main_ui = (type() == TYPE_NORMAL) && !window_->IsFullscreen();
#else
  const bool show_main_ui = (type() == TYPE_NORMAL);
#endif

  command_updater_.UpdateCommandEnabled(IDC_SYNC_BOOKMARKS,
      show_main_ui && profile_->IsSyncAccessible());
}

///////////////////////////////////////////////////////////////////////////////
// Browser, Command and state updating (private):

void Browser::InitCommandState() {
  // All browser commands whose state isn't set automagically some other way
  // (like Back & Forward with initial page load) must have their state
  // initialized here, otherwise they will be forever disabled.

  // Navigation commands
  command_updater_.UpdateCommandEnabled(IDC_RELOAD, true);
  command_updater_.UpdateCommandEnabled(IDC_RELOAD_IGNORING_CACHE, true);

  // Window management commands
  command_updater_.UpdateCommandEnabled(IDC_NEW_WINDOW, true);
  command_updater_.UpdateCommandEnabled(IDC_NEW_INCOGNITO_WINDOW, true);
  command_updater_.UpdateCommandEnabled(IDC_CLOSE_WINDOW, true);
  command_updater_.UpdateCommandEnabled(IDC_NEW_TAB, true);
  command_updater_.UpdateCommandEnabled(IDC_CLOSE_TAB, true);
  command_updater_.UpdateCommandEnabled(IDC_DUPLICATE_TAB, true);
  command_updater_.UpdateCommandEnabled(IDC_RESTORE_TAB, false);
  command_updater_.UpdateCommandEnabled(IDC_FULLSCREEN, true);
  command_updater_.UpdateCommandEnabled(IDC_EXIT, true);
  command_updater_.UpdateCommandEnabled(IDC_TOGGLE_VERTICAL_TABS, true);

  // Page-related commands
  command_updater_.UpdateCommandEnabled(IDC_EMAIL_PAGE_LOCATION, true);
  command_updater_.UpdateCommandEnabled(IDC_PRINT, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_AUTO_DETECT, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_UTF8, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_UTF16LE, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_ISO88591, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_WINDOWS1252, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_GBK, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_GB18030, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_BIG5HKSCS, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_BIG5, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_THAI, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_KOREAN, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_SHIFTJIS, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_ISO2022JP, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_EUCJP, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_ISO885915, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_MACINTOSH, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_ISO88592, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_WINDOWS1250, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_ISO88595, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_WINDOWS1251, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_KOI8R, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_KOI8U, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_ISO88597, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_WINDOWS1253, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_ISO88594, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_ISO885913, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_WINDOWS1257, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_ISO88593, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_ISO885910, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_ISO885914, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_ISO885916, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_WINDOWS1254, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_ISO88596, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_WINDOWS1256, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_ISO88598, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_ISO88598I, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_WINDOWS1255, true);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_WINDOWS1258, true);

  // Clipboard commands
  command_updater_.UpdateCommandEnabled(IDC_CUT, true);
  command_updater_.UpdateCommandEnabled(IDC_COPY, true);
  command_updater_.UpdateCommandEnabled(IDC_PASTE, true);

  // Zoom
  command_updater_.UpdateCommandEnabled(IDC_ZOOM_MENU, true);
  command_updater_.UpdateCommandEnabled(IDC_ZOOM_PLUS, true);
  command_updater_.UpdateCommandEnabled(IDC_ZOOM_NORMAL, true);
  command_updater_.UpdateCommandEnabled(IDC_ZOOM_MINUS, true);

  // Show various bits of UI
  command_updater_.UpdateCommandEnabled(IDC_OPEN_FILE, true);
  command_updater_.UpdateCommandEnabled(IDC_CREATE_SHORTCUTS, false);
  command_updater_.UpdateCommandEnabled(IDC_DEV_TOOLS, true);
  command_updater_.UpdateCommandEnabled(IDC_DEV_TOOLS_CONSOLE, true);
  command_updater_.UpdateCommandEnabled(IDC_DEV_TOOLS_INSPECT, true);
  command_updater_.UpdateCommandEnabled(IDC_TASK_MANAGER, true);
  command_updater_.UpdateCommandEnabled(IDC_SHOW_HISTORY, true);
  command_updater_.UpdateCommandEnabled(IDC_SHOW_BOOKMARK_MANAGER, true);
  command_updater_.UpdateCommandEnabled(IDC_SHOW_EXTENSION_SHELF, true);
  command_updater_.UpdateCommandEnabled(IDC_SHOW_DOWNLOADS, true);
  command_updater_.UpdateCommandEnabled(IDC_HELP_PAGE, true);
  command_updater_.UpdateCommandEnabled(IDC_IMPORT_SETTINGS, true);

#if defined(OS_CHROMEOS)
  command_updater_.UpdateCommandEnabled(IDC_SEARCH, true);
  command_updater_.UpdateCommandEnabled(IDC_SYSTEM_OPTIONS, true);
  command_updater_.UpdateCommandEnabled(IDC_INTERNET_OPTIONS, true);
#endif

  ExtensionsService* extensions_service = profile()->GetExtensionsService();
  bool enable_extensions =
      extensions_service && extensions_service->extensions_enabled();
  command_updater_.UpdateCommandEnabled(IDC_MANAGE_EXTENSIONS,
                                        enable_extensions);

  // Initialize other commands based on the window type.
  bool normal_window = type() == TYPE_NORMAL;
  bool non_devtools_window = type() != TYPE_DEVTOOLS;

  // Navigation commands
  command_updater_.UpdateCommandEnabled(IDC_HOME, normal_window);

  // Window management commands
  command_updater_.UpdateCommandEnabled(IDC_SELECT_NEXT_TAB, normal_window);
  command_updater_.UpdateCommandEnabled(IDC_SELECT_PREVIOUS_TAB,
                                        normal_window);
  command_updater_.UpdateCommandEnabled(IDC_MOVE_TAB_NEXT, normal_window);
  command_updater_.UpdateCommandEnabled(IDC_MOVE_TAB_PREVIOUS, normal_window);
  command_updater_.UpdateCommandEnabled(IDC_SELECT_TAB_0, normal_window);
  command_updater_.UpdateCommandEnabled(IDC_SELECT_TAB_1, normal_window);
  command_updater_.UpdateCommandEnabled(IDC_SELECT_TAB_2, normal_window);
  command_updater_.UpdateCommandEnabled(IDC_SELECT_TAB_3, normal_window);
  command_updater_.UpdateCommandEnabled(IDC_SELECT_TAB_4, normal_window);
  command_updater_.UpdateCommandEnabled(IDC_SELECT_TAB_5, normal_window);
  command_updater_.UpdateCommandEnabled(IDC_SELECT_TAB_6, normal_window);
  command_updater_.UpdateCommandEnabled(IDC_SELECT_TAB_7, normal_window);
  command_updater_.UpdateCommandEnabled(IDC_SELECT_LAST_TAB, normal_window);

  // Page-related commands
  command_updater_.UpdateCommandEnabled(IDC_BOOKMARK_PAGE, normal_window);

  // Clipboard commands
  command_updater_.UpdateCommandEnabled(IDC_COPY_URL, non_devtools_window);

  // Find-in-page
  command_updater_.UpdateCommandEnabled(IDC_FIND, non_devtools_window);
  command_updater_.UpdateCommandEnabled(IDC_FIND_NEXT, non_devtools_window);
  command_updater_.UpdateCommandEnabled(IDC_FIND_PREVIOUS, non_devtools_window);

  // AutoFill
  command_updater_.UpdateCommandEnabled(IDC_AUTOFILL_DEFAULT,
                                        non_devtools_window);

  // Show various bits of UI
  command_updater_.UpdateCommandEnabled(IDC_CLEAR_BROWSING_DATA, normal_window);

  // Initialize other commands whose state changes based on fullscreen mode.
  UpdateCommandsForFullscreenMode(false);
}

void Browser::UpdateCommandsForTabState() {
  TabContents* current_tab = GetSelectedTabContents();
  if (!current_tab)  // May be NULL during tab restore.
    return;

  // Navigation commands
  NavigationController& nc = current_tab->controller();
  command_updater_.UpdateCommandEnabled(IDC_BACK, nc.CanGoBack());
  command_updater_.UpdateCommandEnabled(IDC_FORWARD, nc.CanGoForward());
  command_updater_.UpdateCommandEnabled(IDC_RELOAD,
                                        CanReloadContents(current_tab));
  command_updater_.UpdateCommandEnabled(IDC_RELOAD_IGNORING_CACHE,
                                        CanReloadContents(current_tab));

  // Window management commands
  bool non_app_window = !(type() & TYPE_APP);
  command_updater_.UpdateCommandEnabled(IDC_DUPLICATE_TAB,
      non_app_window && CanDuplicateContentsAt(selected_index()));
  command_updater_.UpdateCommandEnabled(IDC_SELECT_NEXT_TAB,
      non_app_window && tab_count() > 1);
  command_updater_.UpdateCommandEnabled(IDC_SELECT_PREVIOUS_TAB,
      non_app_window && tab_count() > 1);

  // Page-related commands
  window_->SetStarredState(current_tab->is_starred());
  command_updater_.UpdateCommandEnabled(IDC_BOOKMARK_ALL_TABS,
                                        CanBookmarkAllTabs());
  command_updater_.UpdateCommandEnabled(IDC_VIEW_SOURCE,
      current_tab->controller().CanViewSource());
  // Instead of using GetURL here, we use url() (which is the "real" url of the
  // page) from the NavigationEntry because its reflects their origin rather
  // than the display one (returned by GetURL) which may be different (like
  // having "view-source:" on the front).
  NavigationEntry* active_entry = nc.GetActiveEntry();
  bool is_savable_url =
      SavePackage::IsSavableURL(active_entry ? active_entry->url() : GURL());
  command_updater_.UpdateCommandEnabled(IDC_SAVE_PAGE, is_savable_url);
  command_updater_.UpdateCommandEnabled(IDC_ENCODING_MENU, is_savable_url &&
      SavePackage::IsSavableContents(current_tab->contents_mime_type()));
  command_updater_.UpdateCommandEnabled(IDC_EMAIL_PAGE_LOCATION,
      current_tab->ShouldDisplayURL() && current_tab->GetURL().is_valid());

  // Show various bits of UI
  // TODO(pinkerton): Disable app-mode in the model until we implement it
  // on the Mac. Be sure to remove both ifdefs. http://crbug.com/13148
#if !defined(OS_MACOSX)
  command_updater_.UpdateCommandEnabled(IDC_CREATE_SHORTCUTS,
      web_app::IsValidUrl(current_tab->GetURL()));
#endif
}

void Browser::UpdateReloadStopState(bool is_loading, bool force) {
  window_->UpdateReloadStopState(is_loading, force);
  command_updater_.UpdateCommandEnabled(IDC_STOP, is_loading);
}

///////////////////////////////////////////////////////////////////////////////
// Browser, UI update coalescing and handling (private):

void Browser::UpdateToolbar(bool should_restore_state) {
  window_->UpdateToolbar(GetSelectedTabContents(), should_restore_state);
}

void Browser::ScheduleUIUpdate(const TabContents* source,
                               unsigned changed_flags) {
  if (!source)
    return;

  // Do some synchronous updates.
  if (changed_flags & TabContents::INVALIDATE_URL &&
      source == GetSelectedTabContents()) {
    // Only update the URL for the current tab. Note that we do not update
    // the navigation commands since those would have already been updated
    // synchronously by NavigationStateChanged.
    UpdateToolbar(false);
    changed_flags &= ~TabContents::INVALIDATE_URL;
  }
  if (changed_flags & TabContents::INVALIDATE_LOAD) {
    // Update the loading state synchronously. This is so the throbber will
    // immediately start/stop, which gives a more snappy feel. We want to do
    // this for any tab so they start & stop quickly.
    tabstrip_model_.UpdateTabContentsStateAt(
        tabstrip_model_.GetIndexOfController(&source->controller()),
        TabStripModelObserver::LOADING_ONLY);
    // The status bubble needs to be updated during INVALIDATE_LOAD too, but
    // we do that asynchronously by not stripping INVALIDATE_LOAD from
    // changed_flags.
  }

  if (changed_flags & TabContents::INVALIDATE_TITLE && !source->is_loading()) {
    // To correctly calculate whether the title changed while not loading
    // we need to process the update synchronously. This state only matters for
    // the TabStripModel, so we notify the TabStripModel now and notify others
    // asynchronously.
    tabstrip_model_.UpdateTabContentsStateAt(
        tabstrip_model_.GetIndexOfController(&source->controller()),
        TabStripModelObserver::TITLE_NOT_LOADING);
  }

  if (changed_flags & TabContents::INVALIDATE_BOOKMARK_BAR ||
      changed_flags & TabContents::INVALIDATE_EXTENSION_SHELF) {
    window()->ShelfVisibilityChanged();
    changed_flags &= ~(TabContents::INVALIDATE_BOOKMARK_BAR |
                       TabContents::INVALIDATE_EXTENSION_SHELF);
  }

  // If the only updates were synchronously handled above, we're done.
  if (changed_flags == 0)
    return;

  // Save the dirty bits.
  scheduled_updates_[source] |= changed_flags;

  if (chrome_updater_factory_.empty()) {
    // No task currently scheduled, start another.
    MessageLoop::current()->PostDelayedTask(
        FROM_HERE,
        chrome_updater_factory_.NewRunnableMethod(
            &Browser::ProcessPendingUIUpdates),
            kUIUpdateCoalescingTimeMS);
  }
}

void Browser::ProcessPendingUIUpdates() {
#ifndef NDEBUG
  // Validate that all tabs we have pending updates for exist. This is scary
  // because the pending list must be kept in sync with any detached or
  // deleted tabs.
  for (UpdateMap::const_iterator i = scheduled_updates_.begin();
       i != scheduled_updates_.end(); ++i) {
    bool found = false;
    for (int tab = 0; tab < tab_count(); tab++) {
      if (GetTabContentsAt(tab) == i->first) {
        found = true;
        break;
      }
    }
    DCHECK(found);
  }
#endif

  chrome_updater_factory_.RevokeAll();

  for (UpdateMap::const_iterator i = scheduled_updates_.begin();
       i != scheduled_updates_.end(); ++i) {
    // Do not dereference |contents|, it may be out-of-date!
    const TabContents* contents = i->first;
    unsigned flags = i->second;

    if (contents == GetSelectedTabContents()) {
      // Updates that only matter when the tab is selected go here.

      if (flags & TabContents::INVALIDATE_PAGE_ACTIONS)
        window()->GetLocationBar()->UpdatePageActions();

      // Updating the URL happens synchronously in ScheduleUIUpdate.
      if (flags & TabContents::INVALIDATE_LOAD && GetStatusBubble())
        GetStatusBubble()->SetStatus(contents->GetStatusText());

      if (flags & (TabContents::INVALIDATE_TAB |
                   TabContents::INVALIDATE_TITLE)) {
// TODO(pinkerton): Disable app-mode in the model until we implement it
// on the Mac. Be sure to remove both ifdefs. http://crbug.com/13148
#if !defined(OS_MACOSX)
        command_updater_.UpdateCommandEnabled(IDC_CREATE_SHORTCUTS,
            web_app::IsValidUrl(contents->GetURL()));
#endif
        window_->UpdateTitleBar();
      }
    }

    // Updates that don't depend upon the selected state go here.
    if (flags & (TabContents::INVALIDATE_TAB | TabContents::INVALIDATE_TITLE)) {
      tabstrip_model_.UpdateTabContentsStateAt(
          tabstrip_model_.GetIndexOfTabContents(contents),
          TabStripModelObserver::ALL);
    }

    // We don't need to process INVALIDATE_STATE, since that's not visible.
  }

  scheduled_updates_.clear();
}

void Browser::RemoveScheduledUpdatesFor(TabContents* contents) {
  if (!contents)
    return;

  UpdateMap::iterator i = scheduled_updates_.find(contents);
  if (i != scheduled_updates_.end())
    scheduled_updates_.erase(i);
}


///////////////////////////////////////////////////////////////////////////////
// Browser, Getters for UI (private):

StatusBubble* Browser::GetStatusBubble() {
#if !defined(OS_MACOSX)
  // In kiosk mode, we want to always hide the status bubble.
  if (CommandLine::ForCurrentProcess()->HasSwitch(switches::kKioskMode))
    return NULL;
#endif
  return window_ ? window_->GetStatusBubble() : NULL;
}

///////////////////////////////////////////////////////////////////////////////
// Browser, Session restore functions (private):

void Browser::SyncHistoryWithTabs(int index) {
  if (!profile()->HasSessionService())
    return;
  SessionService* session_service = profile()->GetSessionService();
  if (session_service) {
    for (int i = index; i < tab_count(); ++i) {
      TabContents* contents = GetTabContentsAt(i);
      if (contents) {
        session_service->SetTabIndexInWindow(
            session_id(), contents->controller().session_id(), i);
        session_service->SetPinnedState(session_id(),
                                        contents->controller().session_id(),
                                        tabstrip_model_.IsTabPinned(i));
      }
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// Browser, OnBeforeUnload handling (private):

void Browser::ProcessPendingTabs() {
  DCHECK(is_attempting_to_close_browser_);

  if (HasCompletedUnloadProcessing()) {
    // We've finished all the unload events and can proceed to close the
    // browser.
    OnWindowClosing();
    return;
  }

  // Process beforeunload tabs first. When that queue is empty, process
  // unload tabs.
  if (!tabs_needing_before_unload_fired_.empty()) {
    TabContents* tab = *(tabs_needing_before_unload_fired_.begin());
    // Null check render_view_host here as this gets called on a PostTask and
    // the tab's render_view_host may have been nulled out.
    if (tab->render_view_host()) {
      tab->render_view_host()->FirePageBeforeUnload(false);
    } else {
      ClearUnloadState(tab);
    }
  } else if (!tabs_needing_unload_fired_.empty()) {
    // We've finished firing all beforeunload events and can proceed with unload
    // events.
    // TODO(ojan): We should add a call to browser_shutdown::OnShutdownStarting
    // somewhere around here so that we have accurate measurements of shutdown
    // time.
    // TODO(ojan): We can probably fire all the unload events in parallel and
    // get a perf benefit from that in the cases where the tab hangs in it's
    // unload handler or takes a long time to page in.
    TabContents* tab = *(tabs_needing_unload_fired_.begin());
    // Null check render_view_host here as this gets called on a PostTask and
    // the tab's render_view_host may have been nulled out.
    if (tab->render_view_host()) {
      tab->render_view_host()->ClosePage(false, -1, -1);
    } else {
      ClearUnloadState(tab);
    }
  } else {
    NOTREACHED();
  }
}

bool Browser::HasCompletedUnloadProcessing() const {
  return is_attempting_to_close_browser_ &&
      tabs_needing_before_unload_fired_.empty() &&
      tabs_needing_unload_fired_.empty();
}

void Browser::CancelWindowClose() {
  // Closing of window can be canceled from:
  // - canceling beforeunload
  // - disallowing closing from IsClosingPermitted.
  DCHECK(is_attempting_to_close_browser_);
  tabs_needing_before_unload_fired_.clear();
  tabs_needing_unload_fired_.clear();
  is_attempting_to_close_browser_ = false;

  // Inform TabCloseableStateWatcher that closing of window has been canceled.
  TabCloseableStateWatcher* watcher =
      g_browser_process->tab_closeable_state_watcher();
  if (watcher)
    watcher->OnWindowCloseCanceled(this);
}

bool Browser::RemoveFromSet(UnloadListenerSet* set, TabContents* tab) {
  DCHECK(is_attempting_to_close_browser_);

  UnloadListenerSet::iterator iter = std::find(set->begin(), set->end(), tab);
  if (iter != set->end()) {
    set->erase(iter);
    return true;
  }
  return false;
}

void Browser::ClearUnloadState(TabContents* tab) {
  // Closing of browser could be canceled (via IsClosingPermitted) between the
  // time when request was initiated and when this method is called, so check
  // for is_attempting_to_close_browser_ flag before proceeding.
  if (is_attempting_to_close_browser_) {
    RemoveFromSet(&tabs_needing_before_unload_fired_, tab);
    RemoveFromSet(&tabs_needing_unload_fired_, tab);
    ProcessPendingTabs();
  }
}


///////////////////////////////////////////////////////////////////////////////
// Browser, In-progress download termination handling (private):

bool Browser::CanCloseWithInProgressDownloads() {
  if (cancel_download_confirmation_state_ != NOT_PROMPTED) {
    if (cancel_download_confirmation_state_ == WAITING_FOR_RESPONSE) {
      // We need to hear from the user before we can close.
      return false;
    }
    // RESPONSE_RECEIVED case, the user decided to go along with the closing.
    return true;
  }
  // Indicated that normal (non-incognito) downloads are pending.
  bool normal_downloads_are_present = false;
  bool incognito_downloads_are_present = false;
  // If there are no download in-progress, our job is done.
  DownloadManager* download_manager = profile_->GetDownloadManager();
  if (profile_->IsOffTheRecord()) {
    // Browser is incognito and so download_manager if present is for incognito
    // downloads.
    incognito_downloads_are_present =
        (download_manager && download_manager->in_progress_count() != 0);
    // Check original profile.
    download_manager = profile_->GetOriginalProfile()->GetDownloadManager();
  }

  normal_downloads_are_present =
      (download_manager && download_manager->in_progress_count() != 0);
  if (!normal_downloads_are_present && !incognito_downloads_are_present)
    return true;

  if (is_attempting_to_close_browser_)
    return true;

  if ((!normal_downloads_are_present && !profile()->IsOffTheRecord()) ||
      (!incognito_downloads_are_present && profile()->IsOffTheRecord()))
    return true;

  // Let's figure out if we are the last window for our profile.
  // Note that we cannot just use BrowserList::GetBrowserCount as browser
  // windows closing is delayed and the returned count might include windows
  // that are being closed.
  // The browser allowed to be closed only if:
  // 1. It is a regular browser and there are no regular downloads present or
  //    this is not the last regular browser window.
  // 2. It is an incognito browser and there are no incognito downloads present
  //    or this is not the last incognito browser window.
  int count = 0;
  for (BrowserList::const_iterator iter = BrowserList::begin();
       iter != BrowserList::end(); ++iter) {
    // Don't count this browser window or any other in the process of closing.
    if (*iter == this || (*iter)->is_attempting_to_close_browser_)
      continue;

    // Verify that this is not the last non-incognito or incognito browser,
    // depending on the pending downloads.
    if (normal_downloads_are_present && !profile()->IsOffTheRecord() &&
        (*iter)->profile()->IsOffTheRecord())
      continue;
    if (incognito_downloads_are_present && profile()->IsOffTheRecord() &&
        !(*iter)->profile()->IsOffTheRecord())
      continue;

    // We test the original profile, because an incognito browser window keeps
    // the original profile alive (and its DownloadManager).
    // We also need to test explicitly the profile directly so that 2 incognito
    // profiles count as a match.
    if ((*iter)->profile() == profile() ||
        (*iter)->profile()->GetOriginalProfile() == profile())
      count++;
  }
  if (count > 0)
    return true;

  cancel_download_confirmation_state_ = WAITING_FOR_RESPONSE;
  window_->ConfirmBrowserCloseWithPendingDownloads();

  // Return false so the browser does not close.  We'll close if the user
  // confirms in the dialog.
  return false;
}

///////////////////////////////////////////////////////////////////////////////
// Browser, Assorted utility functions (private):

// static
Browser* Browser::GetOrCreateTabbedBrowser(Profile* profile) {
  Browser* browser = BrowserList::FindBrowserWithType(profile, TYPE_NORMAL,
                                                      false);
  if (!browser)
    browser = Browser::Create(profile);
  return browser;
}

bool Browser::HandleCrossAppNavigation(TabContents* source,
                                       const GURL& url,
                                       const GURL& referrer,
                                       WindowOpenDisposition* disposition,
                                       PageTransition::Type transition) {
  // Can be null in unit tests.
  ExtensionsService* service = profile_->GetExtensionsService();
  if (!service)
    return false;

  // Can be null, e.g., when executed in a browser with no tabs.
  if (!source)
    return false;

  // Get the source extension, if any.
  Extension* source_extension = source->extension_app();
  if (!source_extension)
    source_extension = extension_app_;

  // Get the destination URL's extension, if any.
  Extension* destination_extension = service->GetExtensionByURL(url);
  if (!destination_extension)
    destination_extension = service->GetExtensionByWebExtent(url);

  // If they are the same, nothing to do.
  if (source_extension == destination_extension)
    return false;

  // If there is a source extension and the new URL is part of its browse
  // extent, also do nothing.
  if (source_extension && source_extension->browse_extent().ContainsURL(url))
    return false;

  if (destination_extension) {
    // Search for an existing app window for this app.
    for (BrowserList::const_iterator iter = BrowserList::begin();
         iter != BrowserList::end(); ++iter) {
      // Found an app window, open the URL there.
      if ((*iter)->extension_app() == destination_extension) {
        (*iter)->OpenURL(url, referrer, NEW_FOREGROUND_TAB, transition);
        (*iter)->window()->Show();
        return true;
      }
    }

    // If the extension wants to be opened in a window, but there is no
    // existing window, create one, then open the URL there.
    if (destination_extension->launch_container() ==
        Extension::LAUNCH_WINDOW) {
      Browser::OpenApplicationWindow(profile_, destination_extension,
                                     Extension::LAUNCH_WINDOW, url);
      return true;
    }
  }

  // Otherwise, we are opening a normal web page.
  //
  // If our source tab is in an app window, we don't want to open the tab
  // there. Find a normal browser to open it in.
  if (extension_app_) {
    Browser* browser = GetOrCreateTabbedBrowser(profile_);
    browser->OpenURL(url, referrer, NEW_FOREGROUND_TAB, transition);
    browser->window()->Show();
    return true;
  }

  // If our source tab is an app tab, don't allow normal web content to
  // overwrite it.
  if (source->extension_app() && *disposition == CURRENT_TAB)
    *disposition = NEW_FOREGROUND_TAB;

  return false;
}

// static
WindowOpenDisposition Browser::AdjustWindowOpenDispositionForTab(
    bool is_pinned,
    const GURL& url,
    const GURL& referrer,
    PageTransition::Type transition,
    WindowOpenDisposition original_disposition) {
  if (!is_pinned ||
      original_disposition != CURRENT_TAB ||
      (transition != PageTransition::AUTO_BOOKMARK &&
       transition != PageTransition::LINK &&
       transition != PageTransition::TYPED)) {
    return original_disposition;
  }

  bool url_is_http_or_https =
       url.SchemeIs(chrome::kHttpScheme) ||
       url.SchemeIs(chrome::kHttpsScheme);
  bool referrer_is_http_or_https =
       referrer.SchemeIs(chrome::kHttpScheme) ||
       referrer.SchemeIs(chrome::kHttpsScheme);
  bool scheme_matches =
      (url.scheme() == referrer.scheme()) ||
      (url_is_http_or_https && referrer_is_http_or_https);

  // If the host and scheme are the same, then we allow the link to open in
  // the current tab, to make the page feel more web-appy.
  if (url.host() == referrer.host() && scheme_matches)
    return original_disposition;

  return NEW_FOREGROUND_TAB;
}

void Browser::OpenURLAtIndex(TabContents* source,
                             const GURL& url,
                             const GURL& referrer,
                             WindowOpenDisposition disposition,
                             PageTransition::Type transition,
                             int index,
                             int add_types) {
  // TODO(beng): Move all this code into a separate helper that has unit tests.

  // No code for these yet
  DCHECK((disposition != NEW_POPUP) && (disposition != SAVE_TO_DISK));

  TabContents* current_tab = source ? source : GetSelectedTabContents();
  bool source_tab_was_frontmost = (current_tab == GetSelectedTabContents());
  TabContents* new_contents = NULL;

  // Opening a bookmark counts as a user gesture, so we don't need to avoid
  // carpet-bombing here.
  PageTransition::Type baseTransitionType =
    PageTransition::StripQualifier(transition);
  if ((baseTransitionType == PageTransition::TYPED ||
      baseTransitionType == PageTransition::AUTO_BOOKMARK) &&
      current_tab != NULL) {
    RenderViewHostDelegate::BrowserIntegration* delegate = current_tab;
    delegate->OnUserGesture();
  }

  disposition = AdjustWindowOpenDispositionForTab(
      current_tab && IsPinned(current_tab),
      url,
      referrer,
      transition,
      disposition);

  if (HandleCrossAppNavigation(current_tab, url, referrer, &disposition,
                               transition)) {
    // If the source tab was brand new, we can be left with an empty tab which
    // looks ugly. Close it. It is still kinda ugly to have a tab flash visible
    // for a second, then disappear. But I think it is better than having a
    // dead tab just hang around.
    if (source->controller().entry_count() == 0)
      CloseTabContents(source);
    return;
  }

  // If the URL is part of the same web site, then load it in the same
  // SiteInstance (and thus the same process).  This is an optimization to
  // reduce process overhead; it is not necessary for compatibility.  (That is,
  // the new tab will not have script connections to the previous tab, so it
  // does not need to be part of the same SiteInstance or BrowsingInstance.)
  // Default to loading in a new SiteInstance and BrowsingInstance.
  // TODO(creis): should this apply to applications?
  SiteInstance* instance = NULL;
  // Don't use this logic when "--process-per-tab" is specified.
  if (!CommandLine::ForCurrentProcess()->HasSwitch(switches::kProcessPerTab)) {
    if (current_tab) {
      const GURL& current_url = current_tab->GetURL();
      if (SiteInstance::IsSameWebSite(profile_, current_url, url))
        instance = current_tab->GetSiteInstance();
    }
  }

  // If this browser doeesn't support tabs, we can only have one tab so a new
  // tab always goes into a tabbed browser window.
  if (!CanSupportWindowFeature(FEATURE_TABSTRIP) &&
      disposition != CURRENT_TAB && disposition != NEW_WINDOW) {
    // If the disposition is OFF_THE_RECORD we don't want to create a new
    // browser that will itself create another OTR browser. This will result in
    // a browser leak (and crash below because no tab is created or selected).
    if (disposition == OFF_THE_RECORD) {
      OpenURLOffTheRecord(profile_, url);
      return;
    }

    Browser* b = GetOrCreateTabbedBrowser(profile_);
    DCHECK(b);

    // If we have just created a new browser window, make sure we select the
    // tab.
    if (b->tab_count() == 0 && disposition == NEW_BACKGROUND_TAB)
      disposition = NEW_FOREGROUND_TAB;

    b->OpenURL(url, referrer, disposition, transition);
    b->window()->Show();
    return;
  }

  if (profile_->IsOffTheRecord() && disposition == OFF_THE_RECORD)
    disposition = NEW_FOREGROUND_TAB;

  if (disposition == SINGLETON_TAB) {
    ShowSingletonTab(url);
    return;
  } else if (disposition == NEW_WINDOW) {
    Browser* browser = Browser::Create(profile_);
    new_contents = browser->AddTabWithURL(
        url, referrer, transition, index,
        TabStripModel::ADD_SELECTED | add_types, instance, std::string());
    browser->window()->Show();
  } else if ((disposition == CURRENT_TAB) && current_tab) {
    tabstrip_model_.TabNavigating(current_tab, transition);

    bool user_initiated = (PageTransition::StripQualifier(transition) ==
                           PageTransition::AUTO_BOOKMARK);

    if (user_initiated && source_tab_was_frontmost &&
        window_->GetLocationBar()) {
      // Forcibly reset the location bar if the url is going to change in the
      // current tab, since otherwise it won't discard any ongoing user edits,
      // since it doesn't realize this is a user-initiated action.
      window_->GetLocationBar()->Revert();
    }

    current_tab->controller().LoadURL(url, referrer, transition);
    new_contents = current_tab;
    if (GetStatusBubble())
      GetStatusBubble()->Hide();

    // Update the location bar. This is synchronous. We specifically don't
    // update the load state since the load hasn't started yet and updating it
    // will put it out of sync with the actual state like whether we're
    // displaying a favicon, which controls the throbber. If we updated it here,
    // the throbber will show the default favicon for a split second when
    // navigating away from the new tab page.
    ScheduleUIUpdate(current_tab, TabContents::INVALIDATE_URL);
  } else if (disposition == OFF_THE_RECORD) {
    OpenURLOffTheRecord(profile_, url);
    return;
  } else if (disposition != SUPPRESS_OPEN) {
    if (disposition != NEW_BACKGROUND_TAB)
      add_types |= TabStripModel::ADD_SELECTED;
    new_contents = AddTabWithURL(url, referrer, transition, index, add_types,
                                 instance, std::string());
  }

  if (disposition != NEW_BACKGROUND_TAB && source_tab_was_frontmost &&
      new_contents) {
    // Give the focus to the newly navigated tab, if the source tab was
    // front-most.
    new_contents->Focus();
  }
}

void Browser::BuildPopupWindow(TabContents* source,
                               TabContents* new_contents,
                               const gfx::Rect& initial_pos) {
  BuildPopupWindowHelper(source, new_contents, initial_pos,
                         (type_ & TYPE_APP) ? TYPE_APP_POPUP : TYPE_POPUP,
                         profile_, false);
}

void Browser::BuildPopupWindowHelper(TabContents* source,
                                     TabContents* new_contents,
                                     const gfx::Rect& initial_pos,
                                     Browser::Type browser_type,
                                     Profile* profile,
                                     bool start_restored) {
  Browser* browser = new Browser(browser_type, profile);
  browser->set_override_bounds(initial_pos);

  if (start_restored)
    browser->set_maximized_state(MAXIMIZED_STATE_UNMAXIMIZED);

  browser->CreateBrowserWindow();
  browser->tabstrip_model()->AppendTabContents(new_contents, true);
  browser->window()->Show();
}

GURL Browser::GetHomePage() const {
  // --homepage overrides any preferences.
  const CommandLine& command_line = *CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch(switches::kHomePage)) {
    FilePath browser_directory;
    PathService::Get(base::DIR_CURRENT, &browser_directory);
    GURL home_page(URLFixerUpper::FixupRelativeFile(browser_directory,
        command_line.GetSwitchValuePath(switches::kHomePage)));
    if (home_page.is_valid())
      return home_page;
  }

  if (profile_->GetPrefs()->GetBoolean(prefs::kHomePageIsNewTabPage))
    return GURL(chrome::kChromeUINewTabURL);
  GURL home_page(URLFixerUpper::FixupURL(
      profile_->GetPrefs()->GetString(prefs::kHomePage),
      std::string()));
  if (!home_page.is_valid())
    return GURL(chrome::kChromeUINewTabURL);
  return home_page;
}

void Browser::FindInPage(bool find_next, bool forward_direction) {
  ShowFindBar();
  if (find_next) {
    string16 find_text;
#if defined(OS_MACOSX)
    // We always want to search for the contents of the find pasteboard on OS X.
    find_text = GetFindPboardText();
#endif
    GetSelectedTabContents()->StartFinding(find_text,
                                           forward_direction,
                                           false);  // Not case sensitive.
  }
}

void Browser::CloseFrame() {
  window_->Close();
}

void Browser::TabDetachedAtImpl(TabContents* contents, int index,
                                DetachType type) {
  if (type == DETACH_TYPE_DETACH) {
    // Save what the user's currently typed.
    window_->GetLocationBar()->SaveStateToContents(contents);

    if (!tabstrip_model_.closing_all())
      SyncHistoryWithTabs(0);
  }

  contents->set_delegate(NULL);
  RemoveScheduledUpdatesFor(contents);

  if (find_bar_controller_.get() && index == tabstrip_model_.selected_index())
    find_bar_controller_->ChangeTabContents(NULL);

  registrar_.Remove(this, NotificationType::TAB_CONTENTS_DISCONNECTED,
                    Source<TabContents>(contents));
}

// static
void Browser::RegisterAppPrefs(const std::wstring& app_name) {
  // A set of apps that we've already started.
  static std::set<std::wstring>* g_app_names = NULL;

  if (!g_app_names)
    g_app_names = new std::set<std::wstring>;

  // Only register once for each app name.
  if (g_app_names->find(app_name) != g_app_names->end())
    return;
  g_app_names->insert(app_name);

  // We need to register the window position pref.
  std::wstring window_pref(prefs::kBrowserWindowPlacement);
  window_pref.append(L"_");
  window_pref.append(app_name);
  PrefService* prefs = g_browser_process->local_state();
  DCHECK(prefs);

  prefs->RegisterDictionaryPref(window_pref.c_str());
}

// static
bool Browser::RunUnloadEventsHelper(TabContents* contents) {
  // If the TabContents is not connected yet, then there's no unload
  // handler we can fire even if the TabContents has an unload listener.
  // One case where we hit this is in a tab that has an infinite loop
  // before load.
  if (TabHasUnloadListener(contents)) {
    // If the page has unload listeners, then we tell the renderer to fire
    // them. Once they have fired, we'll get a message back saying whether
    // to proceed closing the page or not, which sends us back to this method
    // with the HasUnloadListener bit cleared.
    contents->render_view_host()->FirePageBeforeUnload(false);
    return true;
  }
  return false;
}

void Browser::TabRestoreServiceChanged(TabRestoreService* service) {
  command_updater_.UpdateCommandEnabled(IDC_RESTORE_TAB,
                                        !service->entries().empty());
}

void Browser::TabRestoreServiceDestroyed(TabRestoreService* service) {
  if (!tab_restore_service_)
    return;

  DCHECK_EQ(tab_restore_service_, service);
  tab_restore_service_->RemoveObserver(this);
  tab_restore_service_ = NULL;
}

bool Browser::IsPinned(TabContents* source) {
  int index = tabstrip_model_.GetIndexOfTabContents(source);
  if (index == TabStripModel::kNoTab) {
    NOTREACHED() << "IsPinned called for tab not in our strip";
    return false;
  }
  return tabstrip_model_.IsTabPinned(index);
}
