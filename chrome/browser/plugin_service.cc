// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "build/build_config.h"

#include "chrome/browser/plugin_service.h"

#include "base/command_line.h"
#include "base/path_service.h"
#include "base/string_util.h"
#include "base/thread.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "base/waitable_event.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_thread.h"
#include "chrome/browser/chrome_plugin_host.h"
#include "chrome/browser/extensions/extensions_service.h"
#include "chrome/browser/plugin_updater.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/renderer_host/render_process_host.h"
#include "chrome/common/chrome_plugin_lib.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/default_plugin.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/gpu_plugin.h"
#include "chrome/common/logging_chrome.h"
#include "chrome/common/notification_type.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/pepper_plugin_registry.h"
#include "chrome/common/plugin_messages.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/render_messages.h"
#ifndef DISABLE_NACL
#include "native_client/src/trusted/plugin/nacl_entry_points.h"
#endif
#include "webkit/glue/plugins/plugin_constants_win.h"
#include "webkit/glue/plugins/plugin_list.h"
#include "webkit/glue/plugins/webplugininfo.h"

#if defined(OS_CHROMEOS)
#include "chrome/browser/chromeos/plugin_selection_policy.h"
#endif

#if defined(OS_MACOSX)
static void NotifyPluginsOfActivation() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  for (BrowserChildProcessHost::Iterator iter(ChildProcessInfo::PLUGIN_PROCESS);
       !iter.Done(); ++iter) {
    PluginProcessHost* plugin = static_cast<PluginProcessHost*>(*iter);
    plugin->OnAppActivation();
  }
}
#endif

// static
bool PluginService::enable_chrome_plugins_ = true;

void LoadPluginsFromDiskHook() {
  DCHECK(!BrowserThread::CurrentlyOn(BrowserThread::UI) &&
         !BrowserThread::CurrentlyOn(BrowserThread::IO)) <<
         "Can't load plugins on the IO/UI threads since it's very slow.";
}

// static
void PluginService::InitGlobalInstance(Profile* profile) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  NPAPI::PluginList::Singleton()->SetPluginLoadHook(LoadPluginsFromDiskHook);

  // We first group the plugins and then figure out which groups to disable.
  PluginUpdater::GetPluginUpdater()->DisablePluginGroupsFromPrefs(profile);

  if (CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kDisableOutdatedPlugins)) {
    NPAPI::PluginList::Singleton()->DisableOutdatedPluginGroups();
  }

  // Have Chrome plugins write their data to the profile directory.
  GetInstance()->SetChromePluginDataDir(profile->GetPath());
}

// static
PluginService* PluginService::GetInstance() {
  return Singleton<PluginService>::get();
}

// static
void PluginService::EnableChromePlugins(bool enable) {
  enable_chrome_plugins_ = enable;
}

PluginService::PluginService()
    : main_message_loop_(MessageLoop::current()),
      resource_dispatcher_host_(NULL),
      ui_locale_(g_browser_process->GetApplicationLocale()) {
  RegisterPepperPlugins();

  // Have the NPAPI plugin list search for Chrome plugins as well.
  ChromePluginLib::RegisterPluginsWithNPAPI();

  // Load any specified on the command line as well.
  const CommandLine* command_line = CommandLine::ForCurrentProcess();
  FilePath path = command_line->GetSwitchValuePath(switches::kLoadPlugin);
  if (!path.empty())
    NPAPI::PluginList::Singleton()->AddExtraPluginPath(path);
  path = command_line->GetSwitchValuePath(switches::kExtraPluginDir);
  if (!path.empty())
    NPAPI::PluginList::Singleton()->AddExtraPluginDir(path);

  chrome::RegisterInternalDefaultPlugin();

  // Register the internal Flash and PDF, if available.
  if (!CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDisableInternalFlash) &&
      PathService::Get(chrome::FILE_FLASH_PLUGIN, &path)) {
    NPAPI::PluginList::Singleton()->AddExtraPluginPath(path);
  }

#ifndef DISABLE_NACL
  if (command_line->HasSwitch(switches::kInternalNaCl)) {
    RegisterInternalNaClPlugin();
  }
#endif

#if defined(OS_CHROMEOS)
  plugin_selection_policy_ = new chromeos::PluginSelectionPolicy;
  plugin_selection_policy_->StartInit();
#endif

  chrome::RegisterInternalGPUPlugin();

#if defined(OS_WIN)
  hkcu_key_.Create(
      HKEY_CURRENT_USER, kRegistryMozillaPlugins, KEY_NOTIFY);
  hklm_key_.Create(
      HKEY_LOCAL_MACHINE, kRegistryMozillaPlugins, KEY_NOTIFY);
  if (hkcu_key_.StartWatching()) {
    hkcu_event_.reset(new base::WaitableEvent(hkcu_key_.watch_event()));
    hkcu_watcher_.StartWatching(hkcu_event_.get(), this);
  }

  if (hklm_key_.StartWatching()) {
    hklm_event_.reset(new base::WaitableEvent(hklm_key_.watch_event()));
    hklm_watcher_.StartWatching(hklm_event_.get(), this);
  }
#elif defined(OS_POSIX) && !defined(OS_MACOSX)
  // Also find plugins in a user-specific plugins dir,
  // e.g. ~/.config/chromium/Plugins.
  FilePath user_data_dir;
  if (PathService::Get(chrome::DIR_USER_DATA, &user_data_dir)) {
    NPAPI::PluginList::Singleton()->AddExtraPluginDir(
        user_data_dir.Append("Plugins"));
  }
#endif

  registrar_.Add(this, NotificationType::EXTENSION_LOADED,
                 NotificationService::AllSources());
  registrar_.Add(this, NotificationType::EXTENSION_UNLOADED,
                 NotificationService::AllSources());
#if defined(OS_MACOSX)
  // We need to know when the browser comes forward so we can bring modal plugin
  // windows forward too.
  registrar_.Add(this, NotificationType::APP_ACTIVATED,
                 NotificationService::AllSources());
#endif
  registrar_.Add(this, NotificationType::PLUGIN_ENABLE_STATUS_CHANGED,
                 NotificationService::AllSources());
}

PluginService::~PluginService() {
#if defined(OS_WIN)
  // Release the events since they're owned by RegKey, not WaitableEvent.
  hkcu_watcher_.StopWatching();
  hklm_watcher_.StopWatching();
  hkcu_event_->Release();
  hklm_event_->Release();
#endif
}

void PluginService::LoadChromePlugins(
    ResourceDispatcherHost* resource_dispatcher_host) {
  if (!enable_chrome_plugins_)
    return;

  resource_dispatcher_host_ = resource_dispatcher_host;
  ChromePluginLib::LoadChromePlugins(GetCPBrowserFuncsForBrowser());
}

void PluginService::SetChromePluginDataDir(const FilePath& data_dir) {
  chrome_plugin_data_dir_ = data_dir;
}

const FilePath& PluginService::GetChromePluginDataDir() {
  return chrome_plugin_data_dir_;
}

const std::string& PluginService::GetUILocale() {
  return ui_locale_;
}

PluginProcessHost* PluginService::FindPluginProcess(
    const FilePath& plugin_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  if (plugin_path.value().empty()) {
    NOTREACHED() << "should only be called if we have a plugin to load";
    return NULL;
  }

  for (BrowserChildProcessHost::Iterator iter(ChildProcessInfo::PLUGIN_PROCESS);
       !iter.Done(); ++iter) {
    PluginProcessHost* plugin = static_cast<PluginProcessHost*>(*iter);
    if (plugin->info().path == plugin_path)
      return plugin;
  }

  return NULL;
}

PluginProcessHost* PluginService::FindOrStartPluginProcess(
    const FilePath& plugin_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  PluginProcessHost *plugin_host = FindPluginProcess(plugin_path);
  if (plugin_host)
    return plugin_host;

  WebPluginInfo info;
  if (!NPAPI::PluginList::Singleton()->GetPluginInfoByPath(
          plugin_path, &info)) {
    DCHECK(false);
    return NULL;
  }

  // This plugin isn't loaded by any plugin process, so create a new process.
  plugin_host = new PluginProcessHost();
  if (!plugin_host->Init(info, ui_locale_)) {
    DCHECK(false);  // Init is not expected to fail
    delete plugin_host;
    return NULL;
  }

  return plugin_host;
}

void PluginService::OpenChannelToPlugin(
    const GURL& url,
    const std::string& mime_type,
    PluginProcessHost::Client* client) {
  // The PluginList::GetFirstAllowedPluginInfo may need to load the
  // plugins.  Don't do it on the IO thread.
  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      NewRunnableMethod(
          this, &PluginService::GetAllowedPluginForOpenChannelToPlugin,
          url, mime_type, client));
}

void PluginService::GetAllowedPluginForOpenChannelToPlugin(
    const GURL& url,
    const std::string& mime_type,
    PluginProcessHost::Client* client) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  WebPluginInfo info;
  bool found = GetFirstAllowedPluginInfo(url, mime_type, &info, NULL);
  FilePath plugin_path;
  if (found && info.enabled)
    plugin_path = FilePath(info.path);

  // Now we jump back to the IO thread to finish opening the channel.
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      NewRunnableMethod(
          this, &PluginService::FinishOpenChannelToPlugin,
          plugin_path, client));
}

void PluginService::FinishOpenChannelToPlugin(
    const FilePath& plugin_path,
    PluginProcessHost::Client* client) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  PluginProcessHost* plugin_host = FindOrStartPluginProcess(plugin_path);
  if (plugin_host)
    plugin_host->OpenChannelToPlugin(client);
  else
    client->OnError();
}

bool PluginService::GetFirstAllowedPluginInfo(
    const GURL& url,
    const std::string& mime_type,
    WebPluginInfo* info,
    std::string* actual_mime_type) {
  // GetPluginInfoArray may need to load the plugins, so we need to be
  // on the FILE thread.
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  bool allow_wildcard = true;
#if defined(OS_CHROMEOS)
  std::vector<WebPluginInfo> info_array;
  std::vector<std::string> actual_mime_types;
  NPAPI::PluginList::Singleton()->GetPluginInfoArray(
      url, mime_type, allow_wildcard, &info_array, &actual_mime_types);

  // Now we filter by the plugin selection policy.
  int allowed_index = plugin_selection_policy_->FindFirstAllowed(url,
                                                                 info_array);
  if (!info_array.empty() && allowed_index >= 0) {
    *info = info_array[allowed_index];
    if (actual_mime_type)
      *actual_mime_type = actual_mime_types[allowed_index];
    return true;
  }
  return false;
#else
  return NPAPI::PluginList::Singleton()->GetPluginInfo(
      url, mime_type, allow_wildcard, info, actual_mime_type);
#endif
}

static void PurgePluginListCache(bool reload_pages) {
  for (RenderProcessHost::iterator it = RenderProcessHost::AllHostsIterator();
       !it.IsAtEnd(); it.Advance()) {
    it.GetCurrentValue()->Send(new ViewMsg_PurgePluginListCache(reload_pages));
  }
}

void PluginService::OnWaitableEventSignaled(
    base::WaitableEvent* waitable_event) {
#if defined(OS_WIN)
  if (waitable_event == hkcu_event_.get()) {
    hkcu_key_.StartWatching();
  } else {
    hklm_key_.StartWatching();
  }

  NPAPI::PluginList::Singleton()->RefreshPlugins();
  PurgePluginListCache(true);
#endif  // defined(OS_WIN)
}

static void ForceShutdownPlugin(const FilePath& plugin_path) {
  PluginProcessHost* plugin =
      PluginService::GetInstance()->FindPluginProcess(plugin_path);
  if (plugin)
    plugin->ForceShutdown();
}

void PluginService::Observe(NotificationType type,
                            const NotificationSource& source,
                            const NotificationDetails& details) {
  switch (type.value) {
    case NotificationType::EXTENSION_LOADED: {
      const Extension* extension = Details<const Extension>(details).ptr();
      bool plugins_changed = false;
      for (size_t i = 0; i < extension->plugins().size(); ++i) {
        const Extension::PluginInfo& plugin = extension->plugins()[i];
        NPAPI::PluginList::Singleton()->RefreshPlugins();
        NPAPI::PluginList::Singleton()->AddExtraPluginPath(plugin.path);
        plugins_changed = true;
        if (!plugin.is_public)
          private_plugins_[plugin.path] = extension->url();
      }
      if (plugins_changed)
        PurgePluginListCache(false);
      break;
    }

    case NotificationType::EXTENSION_UNLOADED: {
      const Extension* extension = Details<const Extension>(details).ptr();
      bool plugins_changed = false;
      for (size_t i = 0; i < extension->plugins().size(); ++i) {
        const Extension::PluginInfo& plugin = extension->plugins()[i];
        BrowserThread::PostTask(BrowserThread::IO, FROM_HERE,
                                NewRunnableFunction(&ForceShutdownPlugin,
                                                    plugin.path));
        NPAPI::PluginList::Singleton()->RefreshPlugins();
        NPAPI::PluginList::Singleton()->RemoveExtraPluginPath(plugin.path);
        plugins_changed = true;
        if (!plugin.is_public)
          private_plugins_.erase(plugin.path);
      }
      if (plugins_changed)
        PurgePluginListCache(false);
      break;
    }

#if defined(OS_MACOSX)
    case NotificationType::APP_ACTIVATED: {
      BrowserThread::PostTask(BrowserThread::IO, FROM_HERE,
                              NewRunnableFunction(&NotifyPluginsOfActivation));
      break;
    }
#endif

    case NotificationType::PLUGIN_ENABLE_STATUS_CHANGED: {
      PurgePluginListCache(false);
      break;
    }
    default:
      DCHECK(false);
  }
}

bool PluginService::PrivatePluginAllowedForURL(const FilePath& plugin_path,
                                               const GURL& url) {
  if (url.is_empty())
    return true;  // Caller wants all plugins.

  PrivatePluginMap::iterator it = private_plugins_.find(plugin_path);
  if (it == private_plugins_.end())
    return true;  // This plugin is not private, so it's allowed everywhere.

  // We do a dumb compare of scheme and host, rather than using the domain
  // service, since we only care about this for extensions.
  const GURL& required_url = it->second;
  return (url.scheme() == required_url.scheme() &&
          url.host() == required_url.host());
}

void PluginService::RegisterPepperPlugins() {
  std::vector<PepperPluginInfo> plugins;
  PepperPluginRegistry::GetList(&plugins);
  for (size_t i = 0; i < plugins.size(); ++i) {
    NPAPI::PluginVersionInfo info;
    info.path = plugins[i].path;
    info.product_name = plugins[i].name.empty() ?
        plugins[i].path.BaseName().ToWStringHack() :
        ASCIIToWide(plugins[i].name);
    info.file_description = ASCIIToWide(plugins[i].description);
    info.file_extensions = ASCIIToWide(plugins[i].file_extensions);
    info.file_description = ASCIIToWide(plugins[i].type_descriptions);
    info.mime_types = ASCIIToWide(JoinString(plugins[i].mime_types, '|'));

    // These NPAPI entry points will never be called.  TODO(darin): Come up
    // with a cleaner way to register pepper plugins with the NPAPI PluginList,
    // or perhaps refactor the PluginList to be less specific to NPAPI.
    memset(&info.entry_points, 0, sizeof(info.entry_points));

    NPAPI::PluginList::Singleton()->RegisterInternalPlugin(info);
  }
}
