// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_INSTALL_UI_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_INSTALL_UI_H_

#include <string>
#include <vector>

#include "base/file_path.h"
#include "base/ref_counted.h"
#include "base/scoped_ptr.h"
#include "base/string16.h"
#include "chrome/browser/extensions/image_loading_tracker.h"
#include "gfx/native_widget_types.h"
#include "third_party/skia/include/core/SkBitmap.h"

class Extension;
class ExtensionsService;
class MessageLoop;
class Profile;
class InfoBarDelegate;
class SandboxedExtensionUnpacker;
class TabContents;

// Displays all the UI around extension installation and uninstallation.
class ExtensionInstallUI : public ImageLoadingTracker::Observer {
 public:
  enum PromptType {
    INSTALL_PROMPT = 0,
    UNINSTALL_PROMPT,
    NUM_PROMPT_TYPES
  };

  // A mapping from PromptType to message ID for various dialog content.
  static const int kTitleIds[NUM_PROMPT_TYPES];
  static const int kHeadingIds[NUM_PROMPT_TYPES];
  static const int kButtonIds[NUM_PROMPT_TYPES];

  class Delegate {
   public:
    // We call this method after ConfirmInstall()/ConfirmUninstall() to signal
    // that the installation/uninstallation should continue.
    virtual void InstallUIProceed(bool create_app_shortcut) = 0;

    // We call this method after ConfirmInstall()/ConfirmUninstall() to signal
    // that the installation/uninstallation should stop.
    virtual void InstallUIAbort() = 0;
  };

  explicit ExtensionInstallUI(Profile* profile);

  virtual ~ExtensionInstallUI() {}

  // This is called by the installer to verify whether the installation should
  // proceed. This is declared virtual for testing.
  //
  // We *MUST* eventually call either Proceed() or Abort()
  // on |delegate|.
  virtual void ConfirmInstall(Delegate* delegate, Extension* extension);

  // This is called by the extensions management page to verify whether the
  // uninstallation should proceed. This is declared virtual for testing.
  //
  // We *MUST* eventually call either Proceed() or Abort()
  // on |delegate|.
  virtual void ConfirmUninstall(Delegate* delegate, Extension* extension);

  // Installation was successful. This is declared virtual for testing.
  virtual void OnInstallSuccess(Extension* extension);

  // Installation failed. This is declared virtual for testing.
  virtual void OnInstallFailure(const std::string& error);

  // ImageLoadingTracker::Observer overrides.
  virtual void OnImageLoaded(
      SkBitmap* image, ExtensionResource resource, int index);

  // Show an infobar for a newly-installed theme.  previous_theme_id
  // should be empty if the previous theme was the system/default
  // theme.
  //
  // TODO(akalin): Find a better home for this (and
  // GetNewThemeInstalledInfoBarDelegate()).
  static void ShowThemeInfoBar(
      const std::string& previous_theme_id, bool previous_use_system_theme,
      Extension* new_theme, Profile* profile);

 private:
  // Starts the process of showing a confirmation UI, which is split into two.
  // 1) Set off a 'load icon' task.
  // 2) Handle the load icon response and show the UI (OnImageLoaded).
  void ShowConfirmation(PromptType prompt_type);

#if defined(OS_MACOSX)
  // When an extension is installed on Mac with neither browser action nor
  // page action icons, show an infobar instead of a popup bubble.
  void ShowGenericExtensionInstalledInfoBar(Extension* new_extension);
#endif

  // Returns the delegate to control the browser's info bar. This is
  // within its own function due to its platform-specific nature.
  static InfoBarDelegate* GetNewThemeInstalledInfoBarDelegate(
      TabContents* tab_contents, Extension* new_theme,
      const std::string& previous_theme_id, bool previous_use_system_theme);

  // Implements the showing of the install/uninstall dialog prompt.
  // NOTE: The implementations of this function is platform-specific.
  static void ShowExtensionInstallUIPromptImpl(
      Profile* profile, Delegate* delegate, Extension* extension,
      SkBitmap* icon, const string16& warning, PromptType type);

  // Implements the showing of the new install dialog. The implementations of
  // this function are platform-specific.
  static void ShowExtensionInstallUIPrompt2Impl(
      Profile* profile, Delegate* delegate, Extension* extension,
      SkBitmap* icon, const std::vector<string16>& permissions);

  Profile* profile_;
  MessageLoop* ui_loop_;

  // Used to undo theme installation.
  std::string previous_theme_id_;
  bool previous_use_system_theme_;

  SkBitmap icon_;  // The extensions installation icon.
  Extension* extension_;  // The extension we are showing the UI for.
  Delegate* delegate_;    // The delegate we will call Proceed/Abort on after
                          // confirmation UI.
  PromptType prompt_type_;  // The type of prompt we are going to show.

  // Keeps track of extension images being loaded on the File thread for the
  // purpose of showing the install UI.
  ImageLoadingTracker tracker_;
};

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_INSTALL_UI_H_
