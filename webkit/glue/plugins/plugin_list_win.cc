// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/glue/plugins/plugin_list.h"

#include <tchar.h>

#include <set>

#include "base/basictypes.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/path_service.h"
#include "base/registry.h"
#include "base/scoped_ptr.h"
#include "base/string_number_conversions.h"
#include "base/string_split.h"
#include "base/string_util.h"
#include "webkit/glue/plugins/plugin_constants_win.h"
#include "webkit/glue/plugins/plugin_lib.h"
#include "webkit/glue/webkit_glue.h"

namespace {

const TCHAR kRegistryApps[] =
    _T("Software\\Microsoft\\Windows\\CurrentVersion\\App Paths");
const TCHAR kRegistryFirefox[] = _T("firefox.exe");
const TCHAR kRegistryAcrobat[] = _T("Acrobat.exe");
const TCHAR kRegistryAcrobatReader[] = _T("AcroRd32.exe");
const TCHAR kRegistryWindowsMedia[] = _T("wmplayer.exe");
const TCHAR kRegistryQuickTime[] = _T("QuickTimePlayer.exe");
const TCHAR kRegistryPath[] = _T("Path");
const TCHAR kRegistryFirefoxInstalled[] =
    _T("SOFTWARE\\Mozilla\\Mozilla Firefox");
const TCHAR kRegistryJava[] =
    _T("Software\\JavaSoft\\Java Runtime Environment");
const TCHAR kRegistryBrowserJavaVersion[] = _T("BrowserJavaVersion");
const TCHAR kRegistryCurrentJavaVersion[] = _T("CurrentVersion");
const TCHAR kRegistryJavaHome[] = _T("JavaHome");
const TCHAR kJavaDeploy1[] = _T("npdeploytk.dll");
const TCHAR kJavaDeploy2[] = _T("npdeployjava1.dll");

// The application path where we expect to find plugins.
void GetAppDirectory(std::set<FilePath>* plugin_dirs) {
  FilePath app_path;
  if (!webkit_glue::GetApplicationDirectory(&app_path))
    return;

  app_path = app_path.AppendASCII("plugins");
  plugin_dirs->insert(app_path);
}

// The executable path where we expect to find plugins.
void GetExeDirectory(std::set<FilePath>* plugin_dirs) {
  FilePath exe_path;
  if (!webkit_glue::GetExeDirectory(&exe_path))
    return;

  exe_path = exe_path.AppendASCII("plugins");
  plugin_dirs->insert(exe_path);
}

// Gets the installed path for a registered app.
bool GetInstalledPath(const TCHAR* app, FilePath* out) {
  std::wstring reg_path(kRegistryApps);
  reg_path.append(L"\\");
  reg_path.append(app);

  RegKey key(HKEY_LOCAL_MACHINE, reg_path.c_str(), KEY_READ);
  std::wstring path;
  if (key.ReadValue(kRegistryPath, &path)) {
    *out = FilePath(path);
    return true;
  }

  return false;
}

// Search the registry at the given path and detect plugin directories.
void GetPluginsInRegistryDirectory(
    HKEY root_key,
    const std::wstring& registry_folder,
    std::set<FilePath>* plugin_dirs) {
  for (RegistryKeyIterator iter(root_key, registry_folder.c_str());
       iter.Valid(); ++iter) {
    // Use the registry to gather plugin across the file system.
    std::wstring reg_path = registry_folder;
    reg_path.append(L"\\");
    reg_path.append(iter.Name());
    RegKey key(root_key, reg_path.c_str(), KEY_READ);

    std::wstring path;
    if (key.ReadValue(kRegistryPath, &path))
      plugin_dirs->insert(FilePath(path));
  }
}

// Enumerate through the registry key to find all installed FireFox paths.
// FireFox 3 beta and version 2 can coexist. See bug: 1025003
void GetFirefoxInstalledPaths(std::vector<FilePath>* out) {
  RegistryKeyIterator it(HKEY_LOCAL_MACHINE, kRegistryFirefoxInstalled);
  for (; it.Valid(); ++it) {
    std::wstring full_path = std::wstring(kRegistryFirefoxInstalled) + L"\\" +
                             it.Name() + L"\\Main";
    RegKey key(HKEY_LOCAL_MACHINE, full_path.c_str(), KEY_READ);
    std::wstring install_dir;
    if (!key.ReadValue(L"Install Directory", &install_dir))
      continue;
    out->push_back(FilePath(install_dir));
  }
}

// Get plugin directory locations from the Firefox install path.  This is kind
// of a kludge, but it helps us locate the flash player for users that
// already have it for firefox.  Not having to download yet-another-plugin
// is a good thing.
void GetFirefoxDirectory(std::set<FilePath>* plugin_dirs) {
  std::vector<FilePath> paths;
  GetFirefoxInstalledPaths(&paths);
  for (unsigned int i = 0; i < paths.size(); ++i) {
    plugin_dirs->insert(paths[i].Append(L"plugins"));
  }

  FilePath firefox_app_data_plugin_path;
  if (PathService::Get(base::DIR_APP_DATA, &firefox_app_data_plugin_path)) {
    firefox_app_data_plugin_path =
        firefox_app_data_plugin_path.AppendASCII("Mozilla")
                                    .AppendASCII("plugins");
    plugin_dirs->insert(firefox_app_data_plugin_path);
  }
}

// Hardcoded logic to detect Acrobat plugins locations.
void GetAcrobatDirectory(std::set<FilePath>* plugin_dirs) {
  FilePath path;
  if (!GetInstalledPath(kRegistryAcrobatReader, &path) &&
      !GetInstalledPath(kRegistryAcrobat, &path)) {
    return;
  }

  plugin_dirs->insert(path.Append(L"Browser"));
}

// Hardcoded logic to detect QuickTime plugin location.
void GetQuicktimeDirectory(std::set<FilePath>* plugin_dirs) {
  FilePath path;
  if (GetInstalledPath(kRegistryQuickTime, &path))
    plugin_dirs->insert(path.Append(L"plugins"));
}

// Hardcoded logic to detect Windows Media Player plugin location.
void GetWindowsMediaDirectory(std::set<FilePath>* plugin_dirs) {
  FilePath path;
  if (GetInstalledPath(kRegistryWindowsMedia, &path))
    plugin_dirs->insert(path);

  // If the Windows Media Player Firefox plugin is installed before Firefox,
  // the plugin will get written under PFiles\Plugins on one the drives
  // (usually, but not always, the last letter).
  int size = GetLogicalDriveStrings(0, NULL);
  if (size) {
    scoped_array<wchar_t> strings(new wchar_t[size]);
    if (GetLogicalDriveStrings(size, strings.get())) {
      wchar_t* next_drive = strings.get();
      while (*next_drive) {
        if (GetDriveType(next_drive) == DRIVE_FIXED) {
          FilePath pfiles(next_drive);
          pfiles = pfiles.Append(L"PFiles\\Plugins");
          if (file_util::PathExists(pfiles))
            plugin_dirs->insert(pfiles);
        }
        next_drive = &next_drive[wcslen(next_drive) + 1];
      }
    }
  }
}

// Hardcoded logic to detect Java plugin location.
void GetJavaDirectory(std::set<FilePath>* plugin_dirs) {
  // Load the new NPAPI Java plugin
  // 1. Open the main JRE key under HKLM
  RegKey java_key(HKEY_LOCAL_MACHINE, kRegistryJava, KEY_QUERY_VALUE);

  // 2. Read the current Java version
  std::wstring java_version;
  if (!java_key.ReadValue(kRegistryBrowserJavaVersion, &java_version))
    java_key.ReadValue(kRegistryCurrentJavaVersion, &java_version);

  if (!java_version.empty()) {
    java_key.OpenKey(java_version.c_str(), KEY_QUERY_VALUE);

    // 3. Install path of the JRE binaries is specified in "JavaHome"
    //    value under the Java version key.
    std::wstring java_plugin_directory;
    if (java_key.ReadValue(kRegistryJavaHome, &java_plugin_directory)) {
      // 4. The new plugin resides under the 'bin/new_plugin'
      //    subdirectory.
      DCHECK(!java_plugin_directory.empty());
      java_plugin_directory.append(L"\\bin\\new_plugin");

      // 5. We don't know the exact name of the DLL but it's in the form
      //    NP*.dll so just invoke LoadPlugins on this path.
      plugin_dirs->insert(FilePath(java_plugin_directory));
    }
  }
}

}  // anonymous namespace

namespace NPAPI {

void PluginList::PlatformInit() {
  const CommandLine& command_line = *CommandLine::ForCurrentProcess();
  dont_load_new_wmp_ = command_line.HasSwitch(kUseOldWMPPluginSwitch);
}

void PluginList::GetPluginDirectories(std::vector<FilePath>* plugin_dirs) {
  // We use a set for uniqueness, which we require, over order, which we do not.
  std::set<FilePath> dirs;

  // Load from the application-specific area
  GetAppDirectory(&dirs);

  // Load from the executable area
  GetExeDirectory(&dirs);

  // Load Java
  GetJavaDirectory(&dirs);

  // Load firefox plugins too.  This is mainly to try to locate
  // a pre-installed Flash player.
  GetFirefoxDirectory(&dirs);

  // Firefox hard-codes the paths of some popular plugins to ensure that
  // the plugins are found.  We are going to copy this as well.
  GetAcrobatDirectory(&dirs);
  GetQuicktimeDirectory(&dirs);
  GetWindowsMediaDirectory(&dirs);

  for (std::set<FilePath>::iterator i = dirs.begin(); i != dirs.end(); ++i)
    plugin_dirs->push_back(*i);
}

void PluginList::LoadPluginsFromDir(const FilePath &path,
                                    std::vector<WebPluginInfo>* plugins,
                                    std::set<FilePath>* visited_plugins) {
  WIN32_FIND_DATA find_file_data;
  HANDLE find_handle;

  std::wstring dir = path.value();
  // FindFirstFile requires that you specify a wildcard for directories.
  dir.append(L"\\NP*.DLL");

  find_handle = FindFirstFile(dir.c_str(), &find_file_data);
  if (find_handle == INVALID_HANDLE_VALUE)
    return;

  do {
    if (!(find_file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
      FilePath filename = path.Append(find_file_data.cFileName);
      LoadPlugin(filename, plugins);
      visited_plugins->insert(filename);
    }
  } while (FindNextFile(find_handle, &find_file_data) != 0);

  DCHECK(GetLastError() == ERROR_NO_MORE_FILES);
  FindClose(find_handle);
}

void PluginList::LoadPluginsFromRegistry(
    std::vector<WebPluginInfo>* plugins,
    std::set<FilePath>* visited_plugins) {
  std::set<FilePath> plugin_dirs;

  GetPluginsInRegistryDirectory(
      HKEY_CURRENT_USER, kRegistryMozillaPlugins, &plugin_dirs);
  GetPluginsInRegistryDirectory(
      HKEY_LOCAL_MACHINE, kRegistryMozillaPlugins, &plugin_dirs);

  for (std::set<FilePath>::iterator i = plugin_dirs.begin();
       i != plugin_dirs.end(); ++i) {
    LoadPlugin(*i, plugins);
    visited_plugins->insert(*i);
  }
}

// Returns true if the given plugins share at least one mime type.  This is used
// to differentiate newer versions of a plugin vs two plugins which happen to
// have the same filename.
bool HaveSharedMimeType(const WebPluginInfo& plugin1,
                        const WebPluginInfo& plugin2) {
  for (size_t i = 0; i < plugin1.mime_types.size(); ++i) {
    for (size_t j = 0; j < plugin2.mime_types.size(); ++j) {
      if (plugin1.mime_types[i].mime_type == plugin2.mime_types[j].mime_type)
        return true;
    }
  }

  return false;
}

// Compares Windows style version strings (i.e. 1,2,3,4).  Returns true if b's
// version is newer than a's, or false if it's equal or older.
bool IsNewerVersion(const std::wstring& a, const std::wstring& b) {
  std::vector<std::wstring> a_ver, b_ver;
  SplitString(a, ',', &a_ver);
  SplitString(b, ',', &b_ver);
  if (a_ver.size() == 1 && b_ver.size() == 1) {
    a_ver.clear();
    b_ver.clear();
    SplitString(a, '.', &a_ver);
    SplitString(b, '.', &b_ver);
  }
  if (a_ver.size() != b_ver.size())
    return false;
  for (size_t i = 0; i < a_ver.size(); i++) {
    int cur_a, cur_b;
    base::StringToInt(a_ver[i], &cur_a);
    base::StringToInt(b_ver[i], &cur_b);

    if (cur_a > cur_b)
      return false;
    if (cur_a < cur_b)
      return true;
  }
  return false;
}

bool PluginList::ShouldLoadPlugin(const WebPluginInfo& info,
                                  std::vector<WebPluginInfo>* plugins) {
  // Version check

  for (size_t i = 0; i < plugins->size(); ++i) {
    std::wstring plugin1 =
        StringToLowerASCII((*plugins)[i].path.BaseName().ToWStringHack());
    std::wstring plugin2 =
        StringToLowerASCII(info.path.BaseName().ToWStringHack());
    if ((plugin1 == plugin2 && HaveSharedMimeType((*plugins)[i], info)) ||
        (plugin1 == kJavaDeploy1 && plugin2 == kJavaDeploy2) ||
        (plugin1 == kJavaDeploy2 && plugin2 == kJavaDeploy1)) {
      if (!IsNewerVersion((*plugins)[i].version, info.version))
        return false;  // We have loaded a plugin whose version is newer.

      plugins->erase(plugins->begin() + i);
      break;
    }
  }

  // Troublemakers

  std::wstring filename = StringToLowerASCII(info.path.BaseName().value());
  // Depends on XPCOM.
  if (filename == kMozillaActiveXPlugin)
    return false;

  // Disable the Yahoo Application State plugin as it crashes the plugin
  // process on return from NPObjectStub::OnInvoke. Please refer to
  // http://b/issue?id=1372124 for more information.
  if (filename == kYahooApplicationStatePlugin)
    return false;

  // Disable the WangWang protocol handler plugin (npww.dll) as it crashes
  // chrome during shutdown. Firefox also disables this plugin.
  // Please refer to http://code.google.com/p/chromium/issues/detail?id=3953
  // for more information.
  if (filename == kWanWangProtocolHandlerPlugin)
    return false;

  // We only work with newer versions of the Java plugin which use NPAPI only
  // and don't depend on XPCOM.
  if (filename == kJavaPlugin1 || filename == kJavaPlugin2) {
    std::vector<std::wstring> ver;
    SplitString(info.version, '.', &ver);
    int major, minor, update;
    if (ver.size() == 4 &&
        base::StringToInt(ver[0], &major) &&
        base::StringToInt(ver[1], &minor) &&
        base::StringToInt(ver[2], &update)) {
      if (major == 6 && minor == 0 && update < 120)
        return false;  // Java SE6 Update 11 or older.
    }
  }

  // Special WMP handling

  // If both the new and old WMP plugins exist, only load the new one.
  if (filename == kNewWMPPlugin) {
    if (dont_load_new_wmp_)
      return false;

    for (size_t i = 0; i < plugins->size(); ++i) {
      if ((*plugins)[i].path.BaseName().value() == kOldWMPPlugin) {
        plugins->erase(plugins->begin() + i);
        break;
      }
    }
  } else if (filename == kOldWMPPlugin) {
    for (size_t i = 0; i < plugins->size(); ++i) {
      if ((*plugins)[i].path.BaseName().value() == kNewWMPPlugin)
        return false;
    }
  }

  return true;
}

}  // namespace NPAPI
