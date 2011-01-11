// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/chrome_paths_internal.h"

#include "base/environment.h"
#include "base/file_util.h"
#include "base/path_service.h"
#include "base/scoped_ptr.h"
#include "base/nix/xdg_util.h"

namespace chrome {

// See http://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html
// for a spec on where config files go.  The net effect for most
// systems is we use ~/.config/chromium/ for Chromium and
// ~/.config/google-chrome/ for official builds.
// (This also helps us sidestep issues with other apps grabbing ~/.chromium .)
bool GetDefaultUserDataDirectory(FilePath* result) {
  scoped_ptr<base::Environment> env(base::Environment::Create());
  FilePath config_dir(
      base::nix::GetXDGDirectory(env.get(), "XDG_CONFIG_HOME", ".config"));
#if defined(GOOGLE_CHROME_BUILD)
  *result = config_dir.Append("google-chrome");
#else
  *result = config_dir.Append("chromium");
#endif
  return true;
}

void GetUserCacheDirectory(const FilePath& profile_dir, FilePath* result) {
  // See http://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html
  // for a spec on where cache files go.  Our rule is:
  // - if the user-data-dir in the standard place,
  //     use same subdirectory of the cache directory.
  //     (this maps ~/.config/google-chrome to ~/.cache/google-chrome as well
  //      as the same thing for ~/.config/chromium)
  // - otherwise, use the profile dir directly.

  // Default value in cases where any of the following fails.
  *result = profile_dir;

  scoped_ptr<base::Environment> env(base::Environment::Create());

  FilePath cache_dir;
  if (!PathService::Get(base::DIR_CACHE, &cache_dir))
    return;
  FilePath config_dir(
      base::nix::GetXDGDirectory(env.get(), "XDG_CONFIG_HOME", ".config"));

  if (!config_dir.AppendRelativePath(profile_dir, &cache_dir))
    return;

  *result = cache_dir;
}

bool GetChromeFrameUserDataDirectory(FilePath* result) {
  scoped_ptr<base::Environment> env(base::Environment::Create());
  FilePath config_dir(
      base::nix::GetXDGDirectory(env.get(), "XDG_CONFIG_HOME", ".config"));
#if defined(GOOGLE_CHROME_BUILD)
  *result = config_dir.Append("google-chrome-frame");
#else
  *result = config_dir.Append("chrome-frame");
#endif
  return true;
}

bool GetUserDocumentsDirectory(FilePath* result) {
  scoped_ptr<base::Environment> env(base::Environment::Create());
  *result = base::nix::GetXDGUserDirectory(env.get(), "DOCUMENTS", "Documents");
  return true;
}

// We respect the user's preferred download location, unless it is
// ~ or their desktop directory, in which case we default to ~/Downloads.
bool GetUserDownloadsDirectory(FilePath* result) {
  scoped_ptr<base::Environment> env(base::Environment::Create());
  *result = base::nix::GetXDGUserDirectory(env.get(), "DOWNLOAD", "Downloads");

  FilePath home = file_util::GetHomeDir();
  if (*result == home) {
    *result = home.Append("Downloads");
    return true;
  }

  FilePath desktop;
  GetUserDesktop(&desktop);
  if (*result == desktop) {
    *result = home.Append("Downloads");
  }

  return true;
}

bool GetUserDesktop(FilePath* result) {
  scoped_ptr<base::Environment> env(base::Environment::Create());
  *result = base::nix::GetXDGUserDirectory(env.get(), "DESKTOP", "Desktop");
  return true;
}

}  // namespace chrome
