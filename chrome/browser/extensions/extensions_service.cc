// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extensions_service.h"

#include "base/basictypes.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/histogram.h"
#include "base/stl_util-inl.h"
#include "base/string16.h"
#include "base/string_util.h"
#include "base/time.h"
#include "base/values.h"
#include "base/version.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_thread.h"
#include "chrome/browser/debugger/devtools_manager.h"
#include "chrome/browser/extensions/crx_installer.h"
#include "chrome/browser/extensions/extension_accessibility_api.h"
#include "chrome/browser/extensions/extension_bookmarks_module.h"
#include "chrome/browser/extensions/extension_browser_event_router.h"
#include "chrome/browser/extensions/extension_cookies_api.h"
#include "chrome/browser/extensions/extension_data_deleter.h"
#include "chrome/browser/extensions/extension_dom_ui.h"
#include "chrome/browser/extensions/extension_error_reporter.h"
#include "chrome/browser/extensions/extension_history_api.h"
#include "chrome/browser/extensions/extension_host.h"
#include "chrome/browser/extensions/extension_process_manager.h"
#include "chrome/browser/extensions/extension_updater.h"
#include "chrome/browser/extensions/external_extension_provider.h"
#include "chrome/browser/extensions/external_pref_extension_provider.h"
#include "chrome/browser/net/chrome_url_request_context.h"
#include "chrome/browser/pref_service.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/search_engines/template_url_model.h"
#include "chrome/browser/sync/glue/extension_util.h"
#include "chrome/common/child_process_logging.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_constants.h"
#include "chrome/common/extensions/extension_error_utils.h"
#include "chrome/common/extensions/extension_file_util.h"
#include "chrome/common/extensions/extension_l10n_util.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/notification_type.h"
#include "chrome/common/json_value_serializer.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "googleurl/src/gurl.h"
#include "webkit/database/database_tracker.h"
#include "webkit/database/database_util.h"

#if defined(OS_WIN)
#include "chrome/browser/extensions/external_registry_extension_provider_win.h"
#endif

using base::Time;

namespace errors = extension_manifest_errors;

namespace {

static bool ShouldReloadExtensionManifest(const ExtensionInfo& info) {
  // Always reload LOAD extension manifests, because they can change on disk
  // independent of the manifest in our prefs.
  if (info.extension_location == Extension::LOAD)
    return true;

  // Otherwise, reload the manifest it needs to be relocalized.
  return extension_l10n_util::ShouldRelocalizeManifest(info);
}

}  // namespace

PendingExtensionInfo::PendingExtensionInfo(const GURL& update_url,
                                           bool is_theme,
                                           bool install_silently,
                                           bool enable_on_install,
                                           bool enable_incognito_on_install)
    : update_url(update_url),
      is_theme(is_theme),
      install_silently(install_silently),
      enable_on_install(enable_on_install),
      enable_incognito_on_install(enable_incognito_on_install) {}

PendingExtensionInfo::PendingExtensionInfo()
    : update_url(),
      is_theme(false),
      install_silently(false),
      enable_on_install(false),
      enable_incognito_on_install(false) {}

// ExtensionsService.

const char* ExtensionsService::kInstallDirectoryName = "Extensions";
const char* ExtensionsService::kCurrentVersionFileName = "Current Version";

bool ExtensionsService::IsGalleryDownloadURL(const GURL& download_url) {
  if (StartsWithASCII(download_url.spec(),
                      extension_urls::kMiniGalleryDownloadPrefix, false))
     return true;

  GURL gallery_download_prefix(extension_urls::kGalleryDownloadPrefix);
  if (download_url.host() == gallery_download_prefix.host() &&
      StartsWithASCII(download_url.path(),
                      gallery_download_prefix.path(), false))
    return true;

  // Allow command line gallery url to be referrer for the gallery downloads.
  std::string command_line_gallery_url =
      CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          switches::kAppsGalleryURL);
  if (!command_line_gallery_url.empty() &&
      StartsWithASCII(download_url.spec(),
                      extension_urls::kGalleryDownloadPrefix, false))
    return true;

  return false;
}

// static
bool ExtensionsService::IsDownloadFromGallery(const GURL& download_url,
                                              const GURL& referrer_url) {
  if (!IsGalleryDownloadURL(download_url))
    return false;

  if (StartsWithASCII(referrer_url.spec(),
                      extension_urls::kMiniGalleryBrowsePrefix, false))
    return true;

  if (StartsWithASCII(referrer_url.spec(),
                      Extension::ChromeStoreURL(), false))
    return true;

  // Allow command line gallery url to be referrer for the gallery downloads.
  std::string command_line_gallery_url =
      CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          switches::kAppsGalleryURL);
  if (!command_line_gallery_url.empty() &&
      StartsWithASCII(referrer_url.spec(),
                      command_line_gallery_url, false))
    return true;

  return false;
}

bool ExtensionsService::IsDownloadFromMiniGallery(const GURL& download_url) {
  return StartsWithASCII(download_url.spec(),
                         extension_urls::kMiniGalleryDownloadPrefix,
                         false);  // case_sensitive
}

ExtensionsService::ExtensionsService(Profile* profile,
                                     const CommandLine* command_line,
                                     PrefService* prefs,
                                     const FilePath& install_directory,
                                     bool autoupdate_enabled)
    : profile_(profile),
      extension_prefs_(new ExtensionPrefs(prefs, install_directory)),
      install_directory_(install_directory),
      extensions_enabled_(true),
      show_extensions_prompts_(true),
      ready_(false),
      ALLOW_THIS_IN_INITIALIZER_LIST(toolbar_model_(this)) {
  // Figure out if extension installation should be enabled.
  if (command_line->HasSwitch(switches::kDisableExtensions)) {
    extensions_enabled_ = false;
  } else if (profile->GetPrefs()->GetBoolean(prefs::kDisableExtensions)) {
    extensions_enabled_ = false;
  }

  registrar_.Add(this, NotificationType::EXTENSION_HOST_DID_STOP_LOADING,
                 NotificationService::AllSources());
  registrar_.Add(this, NotificationType::EXTENSION_PROCESS_TERMINATED,
                 Source<Profile>(profile_));

  // Set up the ExtensionUpdater
  if (autoupdate_enabled) {
    int update_frequency = kDefaultUpdateFrequencySeconds;
    if (command_line->HasSwitch(switches::kExtensionsUpdateFrequency)) {
      update_frequency = StringToInt(command_line->GetSwitchValueASCII(
          switches::kExtensionsUpdateFrequency));
    }
    updater_ = new ExtensionUpdater(this, prefs, update_frequency);
  }

  backend_ = new ExtensionsServiceBackend(install_directory_);

  // Use monochrome icons for omnibox icons.
  omnibox_icon_manager_.set_monochrome(true);
}

ExtensionsService::~ExtensionsService() {
  UnloadAllExtensions();
  if (updater_.get()) {
    updater_->Stop();
  }
}

void ExtensionsService::InitEventRouters() {
  ExtensionHistoryEventRouter::GetInstance()->ObserveProfile(profile_);
  ExtensionAccessibilityEventRouter::GetInstance()->ObserveProfile(profile_);
  ExtensionBrowserEventRouter::GetInstance()->Init(profile_);
  ExtensionBookmarkEventRouter::GetSingleton()->Observe(
      profile_->GetBookmarkModel());
  ExtensionCookiesEventRouter::GetInstance()->Init();
}

void ExtensionsService::Init() {
  DCHECK(!ready_);
  DCHECK_EQ(extensions_.size(), 0u);

  // Hack: we need to ensure the ResourceDispatcherHost is ready before we load
  // the first extension, because its members listen for loaded notifications.
  g_browser_process->resource_dispatcher_host();

  LoadAllExtensions();

  // TODO(erikkay) this should probably be deferred to a future point
  // rather than running immediately at startup.
  CheckForExternalUpdates();

  // TODO(erikkay) this should probably be deferred as well.
  GarbageCollectExtensions();
}

void ExtensionsService::InstallExtension(const FilePath& extension_path) {
  scoped_refptr<CrxInstaller> installer(
      new CrxInstaller(install_directory_,
                       this,  // frontend
                       NULL));  // no client (silent install)
  installer->set_allow_privilege_increase(true);
  installer->InstallCrx(extension_path);
}

namespace {
  // TODO(akalin): Put this somewhere where both crx_installer.cc and
  // this file can use it.
  void DeleteFileHelper(const FilePath& path, bool recursive) {
    DCHECK(ChromeThread::CurrentlyOn(ChromeThread::FILE));
    file_util::Delete(path, recursive);
  }
}  // namespace

void ExtensionsService::UpdateExtension(const std::string& id,
                                        const FilePath& extension_path,
                                        const GURL& download_url) {
  PendingExtensionMap::const_iterator it = pending_extensions_.find(id);
  if ((it == pending_extensions_.end()) &&
      !GetExtensionByIdInternal(id, true, true)) {
    LOG(WARNING) << "Will not update extension " << id
                 << " because it is not installed or pending";
    // Delete extension_path since we're not creating a CrxInstaller
    // that would do it for us.
    ChromeThread::PostTask(
        ChromeThread::FILE, FROM_HERE,
        NewRunnableFunction(&DeleteFileHelper, extension_path, false));
    return;
  }

  // We want a silent install only for non-pending extensions and
  // pending extensions that have install_silently set.
  ExtensionInstallUI* client =
      ((it == pending_extensions_.end()) || it->second.install_silently) ?
      NULL : new ExtensionInstallUI(profile_);

  scoped_refptr<CrxInstaller> installer(
      new CrxInstaller(install_directory_,
                       this,  // frontend
                       client));
  installer->set_expected_id(id);
  installer->set_delete_source(true);
  installer->set_limit_web_extent_to_download_host(true);
  installer->set_original_url(download_url);
  installer->InstallCrx(extension_path);
}

void ExtensionsService::AddPendingExtension(
    const std::string& id, const GURL& update_url,
    bool is_theme, bool install_silently,
    bool enable_on_install, bool enable_incognito_on_install) {
  if (GetExtensionByIdInternal(id, true, true)) {
    LOG(DFATAL) << "Trying to add pending extension " << id
                << " which already exists";
    return;
  }
  AddPendingExtensionInternal(
      id, update_url, is_theme, install_silently,
      enable_on_install, enable_incognito_on_install);
}

void ExtensionsService::AddPendingExtensionInternal(
    const std::string& id, const GURL& update_url,
    bool is_theme, bool install_silently,
    bool enable_on_install, bool enable_incognito_on_install) {
  pending_extensions_[id] =
      PendingExtensionInfo(update_url, is_theme, install_silently,
                           enable_on_install, enable_incognito_on_install);
}

void ExtensionsService::ReloadExtension(const std::string& extension_id) {
  FilePath path;
  Extension* current_extension = GetExtensionById(extension_id, false);

  // Unload the extension if it's loaded. It might not be loaded if it crashed.
  if (current_extension) {
    // If the extension has an inspector open for its background page, detach
    // the inspector and hang onto a cookie for it, so that we can reattach
    // later.
    ExtensionProcessManager* manager = profile_->GetExtensionProcessManager();
    ExtensionHost* host = manager->GetBackgroundHostForExtension(
        current_extension);
    if (host) {
      // Look for an open inspector for the background page.
      int devtools_cookie = DevToolsManager::GetInstance()->DetachClientHost(
          host->render_view_host());
      if (devtools_cookie >= 0)
        orphaned_dev_tools_[extension_id] = devtools_cookie;
    }

    path = current_extension->path();
    UnloadExtension(extension_id);
  } else {
    path = unloaded_extension_paths_[extension_id];
  }

  // Check the installed extensions to see if what we're reloading was already
  // installed.
  scoped_ptr<ExtensionInfo> installed_extension(
      extension_prefs_->GetInstalledExtensionInfo(extension_id));
  if (installed_extension.get() &&
      installed_extension->extension_manifest.get()) {
    LoadInstalledExtension(*installed_extension, false);
  } else {
    // We should always be able to remember the extension's path. If it's not in
    // the map, someone failed to update |unloaded_extension_paths_|.
    CHECK(!path.empty());
    LoadExtension(path);
  }
}

void ExtensionsService::UninstallExtension(const std::string& extension_id,
                                           bool external_uninstall) {
  Extension* extension = GetExtensionByIdInternal(extension_id, true, true);

  // Callers should not send us nonexistant extensions.
  DCHECK(extension);

  // Get hold of information we need after unloading, since the extension
  // pointer will be invalid then.
  GURL extension_url(extension->url());
  Extension::Location location(extension->location());

  // Also copy the extension identifier since the reference might have been
  // obtained via Extension::id().
  std::string extension_id_copy(extension_id);

  if (profile_->GetTemplateURLModel())
    profile_->GetTemplateURLModel()->UnregisterExtensionKeyword(extension);

  // Unload before doing more cleanup to ensure that nothing is hanging on to
  // any of these resources.
  UnloadExtension(extension_id);

  extension_prefs_->OnExtensionUninstalled(extension_id_copy, location,
                                           external_uninstall);

  // Tell the backend to start deleting installed extensions on the file thread.
  if (Extension::LOAD != location) {
    ChromeThread::PostTask(
      ChromeThread::FILE, FROM_HERE,
      NewRunnableFunction(
          &extension_file_util::UninstallExtension,
          install_directory_,
          extension_id_copy));
  }

  ClearExtensionData(extension_url);
}

void ExtensionsService::ClearExtensionData(const GURL& extension_url) {
  scoped_refptr<ExtensionDataDeleter> deleter(
      new ExtensionDataDeleter(profile_, extension_url));
  deleter->StartDeleting();
}

void ExtensionsService::EnableExtension(const std::string& extension_id) {
  Extension* extension = GetExtensionByIdInternal(extension_id, false, true);
  if (!extension) {
    NOTREACHED() << "Trying to enable an extension that isn't disabled.";
    return;
  }

  extension_prefs_->SetExtensionState(extension, Extension::ENABLED);

  // Move it over to the enabled list.
  extensions_.push_back(extension);
  ExtensionList::iterator iter = std::find(disabled_extensions_.begin(),
                                           disabled_extensions_.end(),
                                           extension);
  disabled_extensions_.erase(iter);

  ExtensionDOMUI::RegisterChromeURLOverrides(profile_,
      extension->GetChromeURLOverrides());

  NotifyExtensionLoaded(extension);
  UpdateActiveExtensionsInCrashReporter();
}

void ExtensionsService::DisableExtension(const std::string& extension_id) {
  Extension* extension = GetExtensionByIdInternal(extension_id, true, false);
  // The extension may have been disabled already.
  if (!extension)
    return;

  extension_prefs_->SetExtensionState(extension, Extension::DISABLED);

  // Move it over to the disabled list.
  disabled_extensions_.push_back(extension);
  ExtensionList::iterator iter = std::find(extensions_.begin(),
                                           extensions_.end(),
                                           extension);
  extensions_.erase(iter);

  ExtensionDOMUI::UnregisterChromeURLOverrides(profile_,
      extension->GetChromeURLOverrides());

  NotifyExtensionUnloaded(extension);
  UpdateActiveExtensionsInCrashReporter();
}

void ExtensionsService::LoadExtension(const FilePath& extension_path) {
  ChromeThread::PostTask(
      ChromeThread::FILE, FROM_HERE,
      NewRunnableMethod(
          backend_.get(),
          &ExtensionsServiceBackend::LoadSingleExtension,
          extension_path, scoped_refptr<ExtensionsService>(this)));
}

void ExtensionsService::LoadComponentExtensions() {
  for (RegisteredComponentExtensions::iterator it =
           component_extension_manifests_.begin();
       it != component_extension_manifests_.end(); ++it) {
    JSONStringValueSerializer serializer(it->manifest);
    scoped_ptr<Value> manifest(serializer.Deserialize(NULL, NULL));
    if (!manifest.get()) {
      DLOG(ERROR) << "Failed to retrieve manifest for extension";
      continue;
    }

    scoped_ptr<Extension> extension(new Extension(it->root_directory));
    extension->set_location(Extension::COMPONENT);

    std::string error;
    if (!extension->InitFromValue(
            *static_cast<DictionaryValue*>(manifest.get()),
            true,  // require key
            &error)) {
      NOTREACHED() << error;
      return;
    }

    OnExtensionLoaded(extension.release(), false);  // Don't allow privilege
                                                    // increase.
  }
}

void ExtensionsService::LoadAllExtensions() {
  base::TimeTicks start_time = base::TimeTicks::Now();

  // Load any component extensions.
  LoadComponentExtensions();

  // Load the previously installed extensions.
  scoped_ptr<ExtensionPrefs::ExtensionsInfo> info(
      extension_prefs_->GetInstalledExtensionsInfo());

  // If any extensions need localization, we bounce them all to the file thread
  // for re-reading and localization.
  for (size_t i = 0; i < info->size(); ++i) {
    if (ShouldReloadExtensionManifest(*info->at(i))) {
      ChromeThread::PostTask(
        ChromeThread::FILE, FROM_HERE, NewRunnableMethod(
            backend_.get(),
            &ExtensionsServiceBackend::ReloadExtensionManifests,
            info.release(),  // Callee takes ownership of the memory.
            start_time,
            scoped_refptr<ExtensionsService>(this)));
      return;
    }
  }

  // Don't update prefs.
  // Callee takes ownership of the memory.
  ContinueLoadAllExtensions(info.release(), start_time, false);
}

void ExtensionsService::ContinueLoadAllExtensions(
    ExtensionPrefs::ExtensionsInfo* extensions_info,
    base::TimeTicks start_time,
    bool write_to_prefs) {
  scoped_ptr<ExtensionPrefs::ExtensionsInfo> info(extensions_info);

  for (size_t i = 0; i < info->size(); ++i) {
    LoadInstalledExtension(*info->at(i), write_to_prefs);
  }

  OnLoadedInstalledExtensions();

  UMA_HISTOGRAM_COUNTS_100("Extensions.LoadAll", extensions_.size());
  UMA_HISTOGRAM_COUNTS_100("Extensions.Disabled", disabled_extensions_.size());

  UMA_HISTOGRAM_TIMES("Extensions.LoadAllTime",
                      base::TimeTicks::Now() - start_time);

  int user_script_count = 0;
  int extension_count = 0;
  int theme_count = 0;
  int external_count = 0;
  int page_action_count = 0;
  int browser_action_count = 0;
  ExtensionList::iterator ex;
  for (ex = extensions_.begin(); ex != extensions_.end(); ++ex) {
    // Don't count component extensions, since they are only extensions as an
    // implementation detail.
    if ((*ex)->location() == Extension::COMPONENT)
      continue;

    // Don't count unpacked extensions, since they're a developer-specific
    // feature.
    if ((*ex)->location() == Extension::LOAD)
      continue;

    if ((*ex)->is_theme()) {
      theme_count++;
    } else if ((*ex)->converted_from_user_script()) {
      user_script_count++;
    } else {
      extension_count++;
    }
    if (Extension::IsExternalLocation((*ex)->location())) {
      external_count++;
    }
    if ((*ex)->page_action() != NULL) {
      page_action_count++;
    }
    if ((*ex)->browser_action() != NULL) {
      browser_action_count++;
    }
  }
  UMA_HISTOGRAM_COUNTS_100("Extensions.LoadExtension", extension_count);
  UMA_HISTOGRAM_COUNTS_100("Extensions.LoadUserScript", user_script_count);
  UMA_HISTOGRAM_COUNTS_100("Extensions.LoadTheme", theme_count);
  UMA_HISTOGRAM_COUNTS_100("Extensions.LoadExternal", external_count);
  UMA_HISTOGRAM_COUNTS_100("Extensions.LoadPageAction", page_action_count);
  UMA_HISTOGRAM_COUNTS_100("Extensions.LoadBrowserAction",
                           browser_action_count);
}

void ExtensionsService::LoadInstalledExtension(const ExtensionInfo& info,
                                               bool write_to_prefs) {
  std::string error;
  Extension* extension = NULL;
  if (info.extension_manifest.get()) {
    scoped_ptr<Extension> tmp(new Extension(info.extension_path));
    bool require_key = info.extension_location != Extension::LOAD;
    if (tmp->InitFromValue(*info.extension_manifest, require_key, &error))
      extension = tmp.release();
  } else {
    error = errors::kManifestUnreadable;
  }

  if (!extension) {
    ReportExtensionLoadError(info.extension_path,
                             error,
                             NotificationType::EXTENSION_INSTALL_ERROR,
                             false);
    return;
  }

  extension->set_location(info.extension_location);

  if (write_to_prefs)
    extension_prefs_->UpdateManifest(extension);

  OnExtensionLoaded(extension, true);

  if (info.extension_location == Extension::EXTERNAL_PREF ||
      info.extension_location == Extension::EXTERNAL_REGISTRY) {
    ChromeThread::PostTask(
        ChromeThread::FILE, FROM_HERE,
        NewRunnableMethod(
        backend_.get(),
        &ExtensionsServiceBackend::CheckExternalUninstall,
        scoped_refptr<ExtensionsService>(this),
        info.extension_id,
        info.extension_location));
  }
}

void ExtensionsService::NotifyExtensionLoaded(Extension* extension) {
  // The ChromeURLRequestContexts need to be first to know that the extension
  // was loaded, otherwise a race can arise where a renderer that is created
  // for the extension may try to load an extension URL with an extension id
  // that the request context doesn't yet know about. The profile is responsible
  // for ensuring its URLRequestContexts appropriately discover the loaded
  // extension.
  if (profile_) {
    profile_->RegisterExtensionWithRequestContexts(extension);

    // Check if this permission requires unlimited storage quota
    if (extension->HasApiPermission(Extension::kUnlimitedStoragePermission)) {
      string16 origin_identifier =
          webkit_database::DatabaseUtil::GetOriginIdentifier(extension->url());
      ChromeThread::PostTask(
          ChromeThread::FILE, FROM_HERE,
          NewRunnableMethod(
              profile_->GetDatabaseTracker(),
              &webkit_database::DatabaseTracker::SetOriginQuotaInMemory,
              origin_identifier,
              kint64max));
    }
  }

  LOG(INFO) << "Sending EXTENSION_LOADED";

  NotificationService::current()->Notify(
      NotificationType::EXTENSION_LOADED,
      Source<Profile>(profile_),
      Details<Extension>(extension));
}

void ExtensionsService::NotifyExtensionUnloaded(Extension* extension) {
  LOG(INFO) << "Sending EXTENSION_UNLOADED";

  NotificationService::current()->Notify(
      NotificationType::EXTENSION_UNLOADED,
      Source<Profile>(profile_),
      Details<Extension>(extension));

  if (profile_) {
    profile_->UnregisterExtensionWithRequestContexts(extension);

    // Check if this permission required unlimited storage quota, reset its
    // in-memory quota.
    if (extension->HasApiPermission(Extension::kUnlimitedStoragePermission)) {
      string16 origin_identifier =
          webkit_database::DatabaseUtil::GetOriginIdentifier(extension->url());
      ChromeThread::PostTask(
          ChromeThread::FILE, FROM_HERE,
          NewRunnableMethod(
              profile_->GetDatabaseTracker(),
              &webkit_database::DatabaseTracker::ResetOriginQuotaInMemory,
              origin_identifier));
    }
  }
}

void ExtensionsService::UpdateExtensionBlacklist(
  const std::vector<std::string>& blacklist) {
  // Use this set to indicate if an extension in the blacklist has been used.
  std::set<std::string> blacklist_set;
  for (unsigned int i = 0; i < blacklist.size(); ++i) {
    if (Extension::IdIsValid(blacklist[i])) {
      blacklist_set.insert(blacklist[i]);
    }
  }
  extension_prefs_->UpdateBlacklist(blacklist_set);
  std::vector<std::string> to_be_removed;
  // Loop current extensions, unload installed extensions.
  for (ExtensionList::const_iterator iter = extensions_.begin();
       iter != extensions_.end(); ++iter) {
    Extension* extension = (*iter);
    if (blacklist_set.find(extension->id()) != blacklist_set.end()) {
      to_be_removed.push_back(extension->id());
    }
  }

  // UnloadExtension will change the extensions_ list. So, we should
  // call it outside the iterator loop.
  for (unsigned int i = 0; i < to_be_removed.size(); ++i) {
    UnloadExtension(to_be_removed[i]);
  }
}

bool ExtensionsService::IsIncognitoEnabled(const Extension* extension) {
  // If this is a component extension we always allow it to work in incognito
  // mode.
  if (extension->location() == Extension::COMPONENT)
    return true;

  // Check the prefs.
  return extension_prefs_->IsIncognitoEnabled(extension->id());
}

void ExtensionsService::SetIsIncognitoEnabled(Extension* extension,
                                              bool enabled) {
  extension_prefs_->SetIsIncognitoEnabled(extension->id(), enabled);

  // Broadcast unloaded and loaded events to update browser state.
  NotifyExtensionUnloaded(extension);
  NotifyExtensionLoaded(extension);
}

bool ExtensionsService::AllowFileAccess(const Extension* extension) {
  return (CommandLine::ForCurrentProcess()->HasSwitch(
              switches::kDisableExtensionsFileAccessCheck) ||
          extension_prefs_->AllowFileAccess(extension->id()));
}

void ExtensionsService::SetAllowFileAccess(Extension* extension, bool allow) {
  extension_prefs_->SetAllowFileAccess(extension->id(), allow);
  NotificationService::current()->Notify(
      NotificationType::EXTENSION_USER_SCRIPTS_UPDATED,
      Source<Profile>(profile_),
      Details<Extension>(extension));
}

bool ExtensionsService::CanExecuteScriptOnHost(Extension* extension,
                                               const GURL& url,
                                               std::string* error) const {
  // No extensions are allowed to execute script on the gallery because that
  // would allow extensions to manipulate their own install pages.
  if (url.host() == GURL(Extension::ChromeStoreURL()).host()
      && !CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kAllowScriptingGallery)) {
    if (error)
      *error = errors::kCannotScriptGallery;
    return false;
  }

  if (extension->HasHostPermission(url))
      return true;

  if (error) {
    *error = ExtensionErrorUtils::FormatErrorMessage(errors::kCannotAccessPage,
                                                     url.spec());
  }

  return false;
}

void ExtensionsService::CheckForExternalUpdates() {
  // This installs or updates externally provided extensions.
  // TODO(aa): Why pass this list into the provider, why not just filter it
  // later?
  std::set<std::string> killed_extensions;
  extension_prefs_->GetKilledExtensionIds(&killed_extensions);
  ChromeThread::PostTask(
      ChromeThread::FILE, FROM_HERE,
      NewRunnableMethod(
          backend_.get(), &ExtensionsServiceBackend::CheckForExternalUpdates,
          killed_extensions, scoped_refptr<ExtensionsService>(this)));
}

void ExtensionsService::UnloadExtension(const std::string& extension_id) {
  // Make sure the extension gets deleted after we return from this function.
  scoped_ptr<Extension> extension(
      GetExtensionByIdInternal(extension_id, true, true));

  // Callers should not send us nonexistant extensions.
  CHECK(extension.get());

  // Keep information about the extension so that we can reload it later
  // even if it's not permanently installed.
  unloaded_extension_paths_[extension->id()] = extension->path();

  ExtensionDOMUI::UnregisterChromeURLOverrides(profile_,
      extension->GetChromeURLOverrides());

  ExtensionList::iterator iter = std::find(disabled_extensions_.begin(),
                                           disabled_extensions_.end(),
                                           extension.get());
  if (iter != disabled_extensions_.end()) {
    disabled_extensions_.erase(iter);
    NotificationService::current()->Notify(
        NotificationType::EXTENSION_UNLOADED_DISABLED,
        Source<Profile>(profile_),
        Details<Extension>(extension.get()));
    return;
  }

  iter = std::find(extensions_.begin(), extensions_.end(), extension.get());

  // Remove the extension from our list.
  extensions_.erase(iter);

  NotifyExtensionUnloaded(extension.get());
  UpdateActiveExtensionsInCrashReporter();
}

void ExtensionsService::UnloadAllExtensions() {
  STLDeleteContainerPointers(extensions_.begin(), extensions_.end());
  extensions_.clear();

  STLDeleteContainerPointers(disabled_extensions_.begin(),
                             disabled_extensions_.end());
  disabled_extensions_.clear();

  // TODO(erikkay) should there be a notification for this?  We can't use
  // EXTENSION_UNLOADED since that implies that the extension has been disabled
  // or uninstalled, and UnloadAll is just part of shutdown.
}

void ExtensionsService::ReloadExtensions() {
  UnloadAllExtensions();
  LoadAllExtensions();
}

void ExtensionsService::GarbageCollectExtensions() {
  if (extension_prefs_->pref_service()->read_only())
    return;

  scoped_ptr<ExtensionPrefs::ExtensionsInfo> info(
      extension_prefs_->GetInstalledExtensionsInfo());

  std::map<std::string, FilePath> extension_paths;
  for (size_t i = 0; i < info->size(); ++i)
    extension_paths[info->at(i)->extension_id] = info->at(i)->extension_path;

  ChromeThread::PostTask(
      ChromeThread::FILE, FROM_HERE,
      NewRunnableFunction(
          &extension_file_util::GarbageCollectExtensions, install_directory_,
          extension_paths));
}

void ExtensionsService::OnLoadedInstalledExtensions() {
  ready_ = true;
  if (updater_.get()) {
    updater_->Start();
  }
  NotificationService::current()->Notify(
      NotificationType::EXTENSIONS_READY,
      Source<Profile>(profile_),
      NotificationService::NoDetails());
}

void ExtensionsService::OnExtensionLoaded(Extension* extension,
                                          bool allow_privilege_increase) {
  // Ensure extension is deleted unless we transfer ownership.
  scoped_ptr<Extension> scoped_extension(extension);

  // The extension is now loaded, remove its data from unloaded extension map.
  unloaded_extension_paths_.erase(extension->id());

  // TODO(aa): Need to re-evaluate this branch. Does this still make sense now
  // that extensions are enabled by default?
  if (extensions_enabled() ||
      extension->is_theme() ||
      extension->location() == Extension::LOAD ||
      Extension::IsExternalLocation(extension->location())) {
    Extension* old = GetExtensionByIdInternal(extension->id(), true, true);
    if (old) {
      // CrxInstaller should have guaranteed that we aren't downgrading.
      CHECK(extension->version()->CompareTo(*(old->version())) >= 0);

      bool allow_silent_upgrade =
          allow_privilege_increase || !Extension::IsPrivilegeIncrease(
              old, extension);

      // Extensions get upgraded if silent upgrades are allowed, otherwise
      // they get disabled.
      if (allow_silent_upgrade) {
        old->set_being_upgraded(true);
        extension->set_being_upgraded(true);
      }

      // To upgrade an extension in place, unload the old one and
      // then load the new one.
      UnloadExtension(old->id());
      old = NULL;

      if (!allow_silent_upgrade) {
        // Extension has changed permissions significantly. Disable it. We
        // send a notification below.
        extension_prefs_->SetExtensionState(extension, Extension::DISABLED);
        extension_prefs_->SetDidExtensionEscalatePermissions(extension, true);
      }
    }

    switch (extension_prefs_->GetExtensionState(extension->id())) {
      case Extension::ENABLED:
        extensions_.push_back(scoped_extension.release());

        NotifyExtensionLoaded(extension);

        ExtensionDOMUI::RegisterChromeURLOverrides(profile_,
            extension->GetChromeURLOverrides());
        break;
      case Extension::DISABLED:
        disabled_extensions_.push_back(scoped_extension.release());
        NotificationService::current()->Notify(
            NotificationType::EXTENSION_UPDATE_DISABLED,
            Source<Profile>(profile_),
            Details<Extension>(extension));
        break;
      default:
        NOTREACHED();
        break;
    }
  }

  extension->set_being_upgraded(false);

  UpdateActiveExtensionsInCrashReporter();

  if (profile_->GetTemplateURLModel())
    profile_->GetTemplateURLModel()->RegisterExtensionKeyword(extension);

  // Load the icon for omnibox-enabled extensions so it will be ready to display
  // in the URL bar.
  if (!extension->omnibox_keyword().empty())
    omnibox_icon_manager_.LoadIcon(extension);
}

void ExtensionsService::UpdateActiveExtensionsInCrashReporter() {
  std::set<std::string> extension_ids;
  for (size_t i = 0; i < extensions_.size(); ++i) {
    if (!extensions_[i]->is_theme())
      extension_ids.insert(extensions_[i]->id());
  }

  child_process_logging::SetActiveExtensions(extension_ids);
}

void ExtensionsService::OnExtensionInstalled(Extension* extension,
                                             bool allow_privilege_increase) {
  // Ensure extension is deleted unless we transfer ownership.
  scoped_ptr<Extension> scoped_extension(extension);
  Extension::State initial_state = Extension::DISABLED;
  bool initial_enable_incognito = false;
  PendingExtensionMap::iterator it =
      pending_extensions_.find(extension->id());
  if (it != pending_extensions_.end()) {
    PendingExtensionInfo pending_extension_info = it->second;
    pending_extensions_.erase(it);
    it = pending_extensions_.end();
    // Set initial state from pending extension data.
    if (pending_extension_info.is_theme != extension->is_theme()) {
      LOG(WARNING)
          << "Not installing pending extension " << extension->id()
          << " with is_theme = " << extension->is_theme()
          << "; expected is_theme = " << pending_extension_info.is_theme;
      // Delete the extension directory since we're not going to
      // load it.
      ChromeThread::PostTask(
          ChromeThread::FILE, FROM_HERE,
          NewRunnableFunction(&DeleteFileHelper, extension->path(), true));
      return;
    }
    if (!extension->is_theme() &&
        !browser_sync::IsExtensionSyncable(*extension)) {
      // We're an extension installed via sync that is unsyncable,
      // i.e. we may have been syncable previously.  We block these
      // installs.  We'll have to update the clause above if we decide
      // to sync other extension-like things, like apps or user
      // scripts.
      //
      // Note that this creates a small window where a user who tries
      // to download/install an extension that is simultaneously
      // installed via sync (and blocked) will find his download
      // blocked.
      //
      // TODO(akalin): Remove this check once we've put in UI to
      // approve synced extensions.
      LOG(WARNING)
          << "Not installing non-syncable extension " << extension->id();
      // Delete the extension directory since we're not going to
      // load it.
      ChromeThread::PostTask(
          ChromeThread::FILE, FROM_HERE,
          NewRunnableFunction(&DeleteFileHelper, extension->path(), true));
      return;
    }
    if (pending_extension_info.is_theme) {
      DCHECK(pending_extension_info.enable_on_install);
      initial_state = Extension::ENABLED;
      DCHECK(!pending_extension_info.enable_incognito_on_install);
      initial_enable_incognito = false;
    } else {
      initial_state =
          pending_extension_info.enable_on_install ?
          Extension::ENABLED : Extension::DISABLED;
      initial_enable_incognito =
          pending_extension_info.enable_incognito_on_install;
    }
  } else {
    // Make sure we don't enable a disabled extension.
    Extension::State existing_state =
        extension_prefs_->GetExtensionState(extension->id());
    initial_state =
        (existing_state == Extension::DISABLED) ?
        Extension::DISABLED : Extension::ENABLED;
    initial_enable_incognito = false;
  }

  extension_prefs_->OnExtensionInstalled(
      extension, initial_state, initial_enable_incognito);

  // Unpacked extensions start off with file access since they are a developer
  // feature.
  if (extension->location() == Extension::LOAD)
    extension_prefs_->SetAllowFileAccess(extension->id(), true);

  // If the extension is a theme, tell the profile (and therefore ThemeProvider)
  // to apply it.
  if (extension->is_theme()) {
    NotificationService::current()->Notify(
        NotificationType::THEME_INSTALLED,
        Source<Profile>(profile_),
        Details<Extension>(extension));
  } else {
    NotificationService::current()->Notify(
        NotificationType::EXTENSION_INSTALLED,
        Source<Profile>(profile_),
        Details<Extension>(extension));
  }

  // Transfer ownership of |extension| to OnExtensionLoaded.
  OnExtensionLoaded(scoped_extension.release(), allow_privilege_increase);
}

Extension* ExtensionsService::GetExtensionByIdInternal(const std::string& id,
                                                       bool include_enabled,
                                                       bool include_disabled) {
  std::string lowercase_id = StringToLowerASCII(id);
  if (include_enabled) {
    for (ExtensionList::const_iterator iter = extensions_.begin();
        iter != extensions_.end(); ++iter) {
      if ((*iter)->id() == lowercase_id)
        return *iter;
    }
  }
  if (include_disabled) {
    for (ExtensionList::const_iterator iter = disabled_extensions_.begin();
        iter != disabled_extensions_.end(); ++iter) {
      if ((*iter)->id() == lowercase_id)
        return *iter;
    }
  }
  return NULL;
}

Extension* ExtensionsService::GetExtensionByURL(const GURL& url) {
  return url.scheme() != chrome::kExtensionScheme ? NULL :
      GetExtensionById(url.host(), false);
}

Extension* ExtensionsService::GetExtensionByWebExtent(const GURL& url) {
  for (size_t i = 0; i < extensions_.size(); ++i) {
    if (extensions_[i]->web_extent().ContainsURL(url))
      return extensions_[i];
  }
  return NULL;
}

Extension* ExtensionsService::GetExtensionByOverlappingWebExtent(
    const ExtensionExtent& extent) {
  for (size_t i = 0; i < extensions_.size(); ++i) {
    if (extensions_[i]->web_extent().OverlapsWith(extent))
      return extensions_[i];
  }

  return NULL;
}

const SkBitmap& ExtensionsService::GetOmniboxIcon(
    const std::string& extension_id) {
  return omnibox_icon_manager_.GetIcon(extension_id);
}

void ExtensionsService::ClearProvidersForTesting() {
  ChromeThread::PostTask(
      ChromeThread::FILE, FROM_HERE,
      NewRunnableMethod(
          backend_.get(), &ExtensionsServiceBackend::ClearProvidersForTesting));
}

void ExtensionsService::SetProviderForTesting(
    Extension::Location location, ExternalExtensionProvider* test_provider) {
  ChromeThread::PostTask(
      ChromeThread::FILE, FROM_HERE,
      NewRunnableMethod(
          backend_.get(), &ExtensionsServiceBackend::SetProviderForTesting,
          location, test_provider));
}

void ExtensionsService::OnExternalExtensionFound(const std::string& id,
                                                 const std::string& version,
                                                 const FilePath& path,
                                                 Extension::Location location) {
  // Before even bothering to unpack, check and see if we already have this
  // version. This is important because these extensions are going to get
  // installed on every startup.
  Extension* existing = GetExtensionById(id, true);
  scoped_ptr<Version> other(Version::GetVersionFromString(version));
  if (existing) {
    switch (existing->version()->CompareTo(*other)) {
      case -1:  // existing version is older, we should upgrade
        break;
      case 0:  // existing version is same, do nothing
        return;
      case 1:  // existing version is newer, uh-oh
        LOG(WARNING) << "Found external version of extension " << id
                     << "that is older than current version. Current version "
                     << "is: " << existing->VersionString() << ". New version "
                     << "is: " << version << ". Keeping current version.";
        return;
    }
  }

  scoped_refptr<CrxInstaller> installer(
      new CrxInstaller(install_directory_,
                       this,  // frontend
                       NULL));  // no client (silent install)
  installer->set_install_source(location);
  installer->set_expected_id(id);
  installer->set_allow_privilege_increase(true);
  installer->InstallCrx(path);
}

void ExtensionsService::ReportExtensionLoadError(
    const FilePath& extension_path,
    const std::string &error,
    NotificationType type,
    bool be_noisy) {
  NotificationService* service = NotificationService::current();
  service->Notify(type,
                  Source<Profile>(profile_),
                  Details<const std::string>(&error));

  // TODO(port): note that this isn't guaranteed to work properly on Linux.
  std::string path_str = WideToUTF8(extension_path.ToWStringHack());
  std::string message = StringPrintf("Could not load extension from '%s'. %s",
                                     path_str.c_str(), error.c_str());
  ExtensionErrorReporter::GetInstance()->ReportError(message, be_noisy);
}

void ExtensionsService::Observe(NotificationType type,
                                const NotificationSource& source,
                                const NotificationDetails& details) {
  switch (type.value) {
    case NotificationType::EXTENSION_HOST_DID_STOP_LOADING: {
      ExtensionHost* host = Details<ExtensionHost>(details).ptr();
      OrphanedDevTools::iterator iter =
          orphaned_dev_tools_.find(host->extension()->id());
      if (iter == orphaned_dev_tools_.end())
        return;

      DevToolsManager::GetInstance()->AttachClientHost(
          iter->second, host->render_view_host());
      orphaned_dev_tools_.erase(iter);
      break;
    }

    case NotificationType::EXTENSION_PROCESS_TERMINATED: {
      DCHECK_EQ(profile_, Source<Profile>(source).ptr());

      // Unload the entire extension. We want it to be in a consistent state:
      // either fully working or not loaded at all, but never half-crashed.
      UnloadExtension(Details<ExtensionHost>(details).ptr()->extension()->id());
      break;
    }

    default:
      NOTREACHED() << "Unexpected notification type.";
  }
}

bool ExtensionsService::HasApps() {
  if (!extensions_enabled_)
    return false;

  for (ExtensionList::const_iterator it = extensions_.begin();
       it != extensions_.end(); ++it) {
    if ((*it)->is_app())
      return true;
  }

  return false;
}

// ExtensionsServicesBackend

ExtensionsServiceBackend::ExtensionsServiceBackend(
    const FilePath& install_directory)
        : frontend_(NULL),
          install_directory_(install_directory),
          alert_on_error_(false) {
  // TODO(aa): This ends up doing blocking IO on the UI thread because it reads
  // pref data in the ctor and that is called on the UI thread. Would be better
  // to re-read data each time we list external extensions, anyway.
  external_extension_providers_[Extension::EXTERNAL_PREF] =
      linked_ptr<ExternalExtensionProvider>(
          new ExternalPrefExtensionProvider());
#if defined(OS_WIN)
  external_extension_providers_[Extension::EXTERNAL_REGISTRY] =
      linked_ptr<ExternalExtensionProvider>(
          new ExternalRegistryExtensionProvider());
#endif
}

ExtensionsServiceBackend::~ExtensionsServiceBackend() {
}

void ExtensionsServiceBackend::LoadSingleExtension(
    const FilePath& path_in, scoped_refptr<ExtensionsService> frontend) {
  frontend_ = frontend;

  // Explicit UI loads are always noisy.
  alert_on_error_ = true;

  FilePath extension_path = path_in;
  file_util::AbsolutePath(&extension_path);

  LOG(INFO) << "Loading single extension from " <<
      extension_path.BaseName().value();

  std::string error;
  Extension* extension = extension_file_util::LoadExtension(
      extension_path,
      false,  // Don't require id
      &error);

  if (!extension) {
    ReportExtensionLoadError(extension_path, error);
    return;
  }

  extension->set_location(Extension::LOAD);

  // Report this as an installed extension so that it gets remembered in the
  // prefs.
  ChromeThread::PostTask(
      ChromeThread::UI, FROM_HERE,
      NewRunnableMethod(frontend_, &ExtensionsService::OnExtensionInstalled,
                        extension, true));
}

void ExtensionsServiceBackend::ReportExtensionLoadError(
    const FilePath& extension_path, const std::string &error) {
  ChromeThread::PostTask(
      ChromeThread::UI, FROM_HERE,
      NewRunnableMethod(
          frontend_,
          &ExtensionsService::ReportExtensionLoadError, extension_path,
          error, NotificationType::EXTENSION_INSTALL_ERROR, alert_on_error_));
}

bool ExtensionsServiceBackend::LookupExternalExtension(
    const std::string& id, Version** version, Extension::Location* location) {
  scoped_ptr<Version> extension_version;
  for (ProviderMap::const_iterator i = external_extension_providers_.begin();
       i != external_extension_providers_.end(); ++i) {
    const ExternalExtensionProvider* provider = i->second.get();
    extension_version.reset(provider->RegisteredVersion(id, location));
    if (extension_version.get()) {
      if (version)
        *version = extension_version.release();
      return true;
    }
  }
  return false;
}

// Some extensions will autoupdate themselves externally from Chrome.  These
// are typically part of some larger client application package.  To support
// these, the extension will register its location in the the preferences file
// (and also, on Windows, in the registry) and this code will periodically
// check that location for a .crx file, which it will then install locally if
// a new version is available.
void ExtensionsServiceBackend::CheckForExternalUpdates(
    std::set<std::string> ids_to_ignore,
    scoped_refptr<ExtensionsService> frontend) {
  // Note that this installation is intentionally silent (since it didn't
  // go through the front-end).  Extensions that are registered in this
  // way are effectively considered 'pre-bundled', and so implicitly
  // trusted.  In general, if something has HKLM or filesystem access,
  // they could install an extension manually themselves anyway.
  alert_on_error_ = false;
  frontend_ = frontend;

  // Ask each external extension provider to give us a call back for each
  // extension they know about. See OnExternalExtensionFound.
  for (ProviderMap::const_iterator i = external_extension_providers_.begin();
       i != external_extension_providers_.end(); ++i) {
    ExternalExtensionProvider* provider = i->second.get();
    provider->VisitRegisteredExtension(this, ids_to_ignore);
  }
}

void ExtensionsServiceBackend::CheckExternalUninstall(
    scoped_refptr<ExtensionsService> frontend, const std::string& id,
    Extension::Location location) {
  // Check if the providers know about this extension.
  ProviderMap::const_iterator i = external_extension_providers_.find(location);
  if (i == external_extension_providers_.end()) {
    NOTREACHED() << "CheckExternalUninstall called for non-external extension "
                 << location;
    return;
  }

  scoped_ptr<Version> version;
  version.reset(i->second->RegisteredVersion(id, NULL));
  if (version.get())
    return;  // Yup, known extension, don't uninstall.

  // This is an external extension that we don't have registered.  Uninstall.
  ChromeThread::PostTask(
      ChromeThread::UI, FROM_HERE,
      NewRunnableMethod(
          frontend.get(), &ExtensionsService::UninstallExtension, id, true));
}

void ExtensionsServiceBackend::ClearProvidersForTesting() {
  external_extension_providers_.clear();
}

void ExtensionsServiceBackend::SetProviderForTesting(
    Extension::Location location,
    ExternalExtensionProvider* test_provider) {
  DCHECK(test_provider);
  external_extension_providers_[location] =
      linked_ptr<ExternalExtensionProvider>(test_provider);
}

void ExtensionsServiceBackend::OnExternalExtensionFound(
    const std::string& id, const Version* version, const FilePath& path,
    Extension::Location location) {
  ChromeThread::PostTask(
      ChromeThread::UI, FROM_HERE,
      NewRunnableMethod(
          frontend_, &ExtensionsService::OnExternalExtensionFound, id,
          version->GetString(), path, location));
}

void ExtensionsServiceBackend::ReloadExtensionManifests(
    ExtensionPrefs::ExtensionsInfo* extensions_to_reload,
    base::TimeTicks start_time,
    scoped_refptr<ExtensionsService> frontend) {
  frontend_ = frontend;

  for (size_t i = 0; i < extensions_to_reload->size(); ++i) {
    ExtensionInfo* info = extensions_to_reload->at(i).get();
    if (!ShouldReloadExtensionManifest(*info))
      continue;

    // We need to reload original manifest in order to localize properly.
    std::string error;
    scoped_ptr<Extension> extension(extension_file_util::LoadExtension(
        info->extension_path, false, &error));

    if (extension.get())
      extensions_to_reload->at(i)->extension_manifest.reset(
          static_cast<DictionaryValue*>(
              extension->manifest_value()->DeepCopy()));
  }

  // Finish installing on UI thread.
  ChromeThread::PostTask(
      ChromeThread::UI, FROM_HERE,
      NewRunnableMethod(
          frontend_,
          &ExtensionsService::ContinueLoadAllExtensions,
          extensions_to_reload,
          start_time,
          true));
}
