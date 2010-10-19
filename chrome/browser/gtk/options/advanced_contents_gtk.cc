// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/gtk/options/advanced_contents_gtk.h"

#include <sys/types.h>
#include <sys/wait.h>

#include <string>
#include <vector>

#include "app/gtk_signal.h"
#include "app/gtk_util.h"
#include "app/l10n_util.h"
#include "base/basictypes.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "base/file_util.h"
#include "base/path_service.h"
#include "base/process_util.h"
#include "base/string_tokenizer.h"
#include "base/xdg_util.h"
#include "chrome/browser/browser_list.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/download/download_manager.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/fonts_languages_window.h"
#include "chrome/browser/gtk/accessible_widget_helper_gtk.h"
#include "chrome/browser/gtk/clear_browsing_data_dialog_gtk.h"
#include "chrome/browser/gtk/gtk_chrome_link_button.h"
#include "chrome/browser/gtk/gtk_util.h"
#include "chrome/browser/gtk/options/content_settings_window_gtk.h"
#include "chrome/browser/gtk/options/options_layout_gtk.h"
#include "chrome/browser/net/predictor_api.h"
#include "chrome/browser/options_page_base.h"
#include "chrome/browser/options_util.h"
#include "chrome/browser/prefs/pref_member.h"
#include "chrome/browser/prefs/pref_set_observer.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/renderer_host/resource_dispatcher_host.h"
#include "chrome/browser/safe_browsing/safe_browsing_service.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/process_watcher.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"

namespace {

// Command used to configure GNOME proxy settings. The command was renamed
// in January 2009, so both are used to work on both old and new systems.
const char* kOldGNOMEProxyConfigCommand[] = {"gnome-network-preferences", NULL};
const char* kGNOMEProxyConfigCommand[] = {"gnome-network-properties", NULL};
// KDE3 and KDE4 are only slightly different, but incompatible. Go figure.
const char* kKDE3ProxyConfigCommand[] = {"kcmshell", "proxy", NULL};
const char* kKDE4ProxyConfigCommand[] = {"kcmshell4", "proxy", NULL};

// The URL for Linux ssl certificate configuration help.
const char* const kLinuxCertificatesConfigUrl =
    "http://code.google.com/p/chromium/wiki/LinuxCertManagement";

// The URL for Linux proxy configuration help when not running under a
// supported desktop environment.
const char kLinuxProxyConfigUrl[] = "about:linux-proxy-config";

// The pixel width we wrap labels at.
// TODO(evanm): make the labels wrap at the appropriate width.
#if defined(OS_CHROMEOS)
// ChromeOS uses IDS_OPTIONS_DIALOG_WIDTH_CHARS for options dialog width, which
// is slightly smaller than the Gtk options dialog's 500px.
const int kWrapWidth = 445;
#else
const int kWrapWidth = 475;
#endif

GtkWidget* CreateWrappedLabel(int string_id) {
  GtkWidget* label = gtk_label_new(
      l10n_util::GetStringUTF8(string_id).c_str());
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_util::SetLabelWidth(label, kWrapWidth);
  return label;
}

GtkWidget* CreateCheckButtonWithWrappedLabel(int string_id) {
  GtkWidget* checkbox = gtk_check_button_new();
  gtk_container_add(GTK_CONTAINER(checkbox),
                    CreateWrappedLabel(string_id));
  return checkbox;
}

GtkWidget* AddCheckButtonWithWrappedLabel(int string_id,
                                          GtkWidget* container,
                                          GCallback handler,
                                          gpointer data) {
  GtkWidget* checkbox = CreateCheckButtonWithWrappedLabel(string_id);
  gtk_box_pack_start(GTK_BOX(container), checkbox, FALSE, FALSE, 0);
  g_signal_connect(checkbox, "toggled", handler, data);

  return checkbox;
}

// Don't let the widget handle scroll events. Instead, pass it on to the
// parent widget.
gboolean PassScrollToParent(GtkWidget* widget, GdkEvent* event,
                            gpointer unused) {
  if (widget->parent)
    gtk_propagate_event(widget->parent, event);

  return TRUE;
}

// Recursively search for a combo box among the children of |widget|.
void SearchForComboBox(GtkWidget* widget, gpointer data) {
  if (GTK_IS_COMBO_BOX(widget)) {
    *reinterpret_cast<GtkWidget**>(data) = widget;
  } else if (GTK_IS_CONTAINER(widget)) {
    gtk_container_foreach(GTK_CONTAINER(widget), SearchForComboBox, data);
  }
}

// Letting the combo boxes in the advanced options page handle scroll events is
// annoying because they fight with the scrolled window. Also,
// GtkFileChooserButton is buggy in that if you scroll on it very quickly it
// spews Gtk-WARNINGs, which causes us to crash in debug. This function disables
// scrolling for the combo box in |widget| (the first one it finds in a DFS).
void DisableScrolling(GtkWidget* widget) {
  gpointer combo_box_ptr = NULL;
  SearchForComboBox(widget, &combo_box_ptr);

  if (!combo_box_ptr) {
    NOTREACHED() << " Did not find a combo box in this widget.";
    return;
  }

  g_signal_connect(GTK_WIDGET(combo_box_ptr), "scroll-event",
                   G_CALLBACK(PassScrollToParent), NULL);
}

}  // anonymous namespace


///////////////////////////////////////////////////////////////////////////////
// DownloadSection

class DownloadSection : public OptionsPageBase {
 public:
  explicit DownloadSection(Profile* profile);
  virtual ~DownloadSection() {}

  GtkWidget* get_page_widget() const {
    return page_;
  }

 private:
  // Overridden from OptionsPageBase.
  virtual void NotifyPrefChanged(const std::string* pref_name);

  // Callbacks for the widgets.
  static void OnDownloadLocationChanged(GtkFileChooser* widget,
                                        DownloadSection* section);
  static void OnDownloadAskForSaveLocationChanged(GtkWidget* widget,
                                                  DownloadSection* section);
  static void OnResetFileHandlersClicked(GtkButton *button,
                                         DownloadSection* section);

  // The widgets for the download options.
  GtkWidget* download_location_button_;
  GtkWidget* download_ask_for_save_location_checkbox_;
  GtkWidget* reset_file_handlers_label_;
  GtkWidget* reset_file_handlers_button_;

  // The widget containing the options for this section.
  GtkWidget* page_;

  // Pref members.
  FilePathPrefMember default_download_location_;
  BooleanPrefMember ask_for_save_location_;
  StringPrefMember auto_open_files_;

  // Flag to ignore gtk callbacks while we are loading prefs, to avoid
  // then turning around and saving them again.
  bool pref_changing_;

  scoped_ptr<AccessibleWidgetHelper> accessible_widget_helper_;

  DISALLOW_COPY_AND_ASSIGN(DownloadSection);
};

DownloadSection::DownloadSection(Profile* profile)
    : OptionsPageBase(profile),
      pref_changing_(true) {
  page_ = gtk_vbox_new(FALSE, gtk_util::kControlSpacing);

  accessible_widget_helper_.reset(new AccessibleWidgetHelper(
      page_, profile));

  // Download location options.
  download_location_button_ = gtk_file_chooser_button_new(
      l10n_util::GetStringUTF8(
          IDS_OPTIONS_DOWNLOADLOCATION_BROWSE_WINDOW_TITLE).c_str(),
      GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
  g_signal_connect(download_location_button_, "selection-changed",
                   G_CALLBACK(OnDownloadLocationChanged), this);
  DisableScrolling(download_location_button_);

  // Add the default download path to the list of shortcuts in the selector.
  FilePath default_download_path;
  if (!PathService::Get(chrome::DIR_DEFAULT_DOWNLOADS,
                        &default_download_path)) {
    NOTREACHED();
  } else {
    if (!gtk_file_chooser_add_shortcut_folder(
        GTK_FILE_CHOOSER(download_location_button_),
        default_download_path.value().c_str(),
        NULL)) {
      NOTREACHED();
    }
  }

  GtkWidget* download_location_control = gtk_util::CreateLabeledControlsGroup(
      NULL,
      l10n_util::GetStringUTF8(
          IDS_OPTIONS_DOWNLOADLOCATION_BROWSE_TITLE).c_str(),
      download_location_button_,
      NULL);
  gtk_box_pack_start(GTK_BOX(page_), download_location_control,
                     FALSE, FALSE, 0);

  download_ask_for_save_location_checkbox_ = CreateCheckButtonWithWrappedLabel(
      IDS_OPTIONS_DOWNLOADLOCATION_ASKFORSAVELOCATION);
  gtk_box_pack_start(GTK_BOX(page_), download_ask_for_save_location_checkbox_,
                     FALSE, FALSE, 0);
  g_signal_connect(download_ask_for_save_location_checkbox_, "clicked",
                   G_CALLBACK(OnDownloadAskForSaveLocationChanged), this);
  accessible_widget_helper_->SetWidgetName(
      download_ask_for_save_location_checkbox_,
      IDS_OPTIONS_DOWNLOADLOCATION_ASKFORSAVELOCATION);

  // Option for resetting file handlers.
  reset_file_handlers_label_ = CreateWrappedLabel(
      IDS_OPTIONS_AUTOOPENFILETYPES_INFO);
  gtk_misc_set_alignment(GTK_MISC(reset_file_handlers_label_), 0, 0);
  gtk_box_pack_start(GTK_BOX(page_), reset_file_handlers_label_,
                     FALSE, FALSE, 0);

  reset_file_handlers_button_ = gtk_button_new_with_label(
      l10n_util::GetStringUTF8(
          IDS_OPTIONS_AUTOOPENFILETYPES_RESETTODEFAULT).c_str());
  g_signal_connect(reset_file_handlers_button_, "clicked",
                   G_CALLBACK(OnResetFileHandlersClicked), this);
  // Stick it in an hbox so it doesn't expand to the whole width.
  GtkWidget* button_hbox = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(button_hbox),
                     reset_file_handlers_button_,
                     FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(page_),
                     gtk_util::IndentWidget(button_hbox),
                     FALSE, FALSE, 0);

  // Init prefs watchers.
  default_download_location_.Init(prefs::kDownloadDefaultDirectory,
                                  profile->GetPrefs(), this);
  ask_for_save_location_.Init(prefs::kPromptForDownload,
                              profile->GetPrefs(), this);
  auto_open_files_.Init(prefs::kDownloadExtensionsToOpen, profile->GetPrefs(),
                        this);

  NotifyPrefChanged(NULL);
}

void DownloadSection::NotifyPrefChanged(const std::string* pref_name) {
  pref_changing_ = true;
  if (!pref_name || *pref_name == prefs::kDownloadDefaultDirectory) {
    gtk_file_chooser_set_current_folder(
        GTK_FILE_CHOOSER(download_location_button_),
            default_download_location_.GetValue().value().c_str());
  }

  if (!pref_name || *pref_name == prefs::kPromptForDownload) {
    gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(download_ask_for_save_location_checkbox_),
        ask_for_save_location_.GetValue());
  }

  if (!pref_name || *pref_name == prefs::kDownloadExtensionsToOpen) {
    DownloadPrefs* prefs = profile()->GetDownloadManager()->download_prefs();
    bool enabled = prefs->IsAutoOpenUsed();
    gtk_widget_set_sensitive(reset_file_handlers_label_, enabled);
    gtk_widget_set_sensitive(reset_file_handlers_button_, enabled);
  }
  pref_changing_ = false;
}

// static
void DownloadSection::OnDownloadLocationChanged(GtkFileChooser* widget,
                                                DownloadSection* section) {
  if (section->pref_changing_)
    return;

  gchar* folder = gtk_file_chooser_get_filename(widget);
  FilePath path(folder);
  g_free(folder);
  // Gtk seems to call this signal multiple times, so we only set the pref and
  // metric if something actually changed.
  if (path != section->default_download_location_.GetValue()) {
    section->default_download_location_.SetValue(path);
    section->UserMetricsRecordAction(
        UserMetricsAction("Options_SetDownloadDirectory"),
        section->profile()->GetPrefs());
  }
}

// static
void DownloadSection::OnDownloadAskForSaveLocationChanged(
    GtkWidget* widget, DownloadSection* section) {
  if (section->pref_changing_)
    return;
  bool enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  if (enabled) {
    section->UserMetricsRecordAction(
        UserMetricsAction("Options_AskForSaveLocation_Enable"),
        section->profile()->GetPrefs());
  } else {
    section->UserMetricsRecordAction(
        UserMetricsAction("Options_AskForSaveLocation_Disable"),
        section->profile()->GetPrefs());
  }
  section->ask_for_save_location_.SetValue(enabled);
}

// static
void DownloadSection::OnResetFileHandlersClicked(GtkButton *button,
                                                 DownloadSection* section) {
  section->profile()->GetDownloadManager()->download_prefs()->ResetAutoOpen();
  section->UserMetricsRecordAction(
      UserMetricsAction("Options_ResetAutoOpenFiles"),
      section->profile()->GetPrefs());
}

///////////////////////////////////////////////////////////////////////////////
// NetworkSection

class NetworkSection : public OptionsPageBase {
 public:
  explicit NetworkSection(Profile* profile);
  virtual ~NetworkSection() {}

  GtkWidget* get_page_widget() const {
    return page_;
  }

 private:
  // Overridden from OptionsPageBase.
  virtual void NotifyPrefChanged(const std::string* pref_name);

  struct ProxyConfigCommand {
    std::string binary;
    const char** argv;
  };
  // The callback functions for invoking the proxy config dialog.
  static void OnChangeProxiesButtonClicked(GtkButton *button,
                                           NetworkSection* section);
  // Search $PATH to find one of the commands. Store the full path to
  // it in the |binary| field and the command array index in in |index|.
  static bool SearchPATH(ProxyConfigCommand* commands, size_t ncommands,
                         size_t* index);
  // Start the given proxy configuration utility.
  static void StartProxyConfigUtil(const ProxyConfigCommand& command);

  // Tracks the state of proxy preferences.
  scoped_ptr<PrefSetObserver> proxy_prefs_;

  // The widget containing the options for this section.
  GtkWidget* page_;

  // The proxy configuration button.
  GtkWidget* change_proxies_button_;

  DISALLOW_COPY_AND_ASSIGN(NetworkSection);
};

NetworkSection::NetworkSection(Profile* profile)
    : OptionsPageBase(profile) {
  page_ = gtk_vbox_new(FALSE, gtk_util::kControlSpacing);

  GtkWidget* proxy_description_label = CreateWrappedLabel(
      IDS_OPTIONS_PROXIES_LABEL);
  gtk_misc_set_alignment(GTK_MISC(proxy_description_label), 0, 0);
  gtk_box_pack_start(GTK_BOX(page_), proxy_description_label,
                     FALSE, FALSE, 0);

  change_proxies_button_ = gtk_button_new_with_label(
      l10n_util::GetStringUTF8(
          IDS_OPTIONS_PROXIES_CONFIGURE_BUTTON).c_str());
  g_signal_connect(change_proxies_button_, "clicked",
                   G_CALLBACK(OnChangeProxiesButtonClicked), this);

  // Stick it in an hbox so it doesn't expand to the whole width.
  GtkWidget* button_hbox = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(button_hbox),
                     change_proxies_button_,
                     FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(page_),
                     gtk_util::IndentWidget(button_hbox),
                     FALSE, FALSE, 0);

  proxy_prefs_.reset(PrefSetObserver::CreateProxyPrefSetObserver(
      profile->GetPrefs(), this));
  NotifyPrefChanged(NULL);
}

void NetworkSection::NotifyPrefChanged(const std::string* pref_name) {
  if (!pref_name || proxy_prefs_->IsObserved(*pref_name))
    gtk_widget_set_sensitive(change_proxies_button_,
                             !proxy_prefs_->IsManaged());
}

// static
void NetworkSection::OnChangeProxiesButtonClicked(GtkButton *button,
                                                  NetworkSection* section) {
  section->UserMetricsRecordAction(UserMetricsAction("Options_ChangeProxies"),
                                   NULL);

  scoped_ptr<base::Environment> env(base::Environment::Create());

  ProxyConfigCommand command;
  bool found_command = false;
  switch (base::GetDesktopEnvironment(env.get())) {
    case base::DESKTOP_ENVIRONMENT_GNOME: {
      size_t index;
      ProxyConfigCommand commands[2];
      commands[0].argv = kGNOMEProxyConfigCommand;
      commands[1].argv = kOldGNOMEProxyConfigCommand;
      found_command = SearchPATH(commands, 2, &index);
      if (found_command)
        command = commands[index];
      break;
    }

    case base::DESKTOP_ENVIRONMENT_KDE3:
      command.argv = kKDE3ProxyConfigCommand;
      found_command = SearchPATH(&command, 1, NULL);
      break;

    case base::DESKTOP_ENVIRONMENT_KDE4:
      command.argv = kKDE4ProxyConfigCommand;
      found_command = SearchPATH(&command, 1, NULL);
      break;

    case base::DESKTOP_ENVIRONMENT_XFCE:
    case base::DESKTOP_ENVIRONMENT_OTHER:
      break;
  }

  if (found_command) {
    StartProxyConfigUtil(command);
  } else {
    const char* name = base::GetDesktopEnvironmentName(env.get());
    if (name)
      LOG(ERROR) << "Could not find " << name << " network settings in $PATH";
    BrowserList::GetLastActive()->
        OpenURL(GURL(kLinuxProxyConfigUrl),
                GURL(), NEW_FOREGROUND_TAB, PageTransition::LINK);
  }
}

// static
bool NetworkSection::SearchPATH(ProxyConfigCommand* commands, size_t ncommands,
                                size_t* index) {
  const char* path = getenv("PATH");
  if (!path)
    return false;
  FilePath bin_path;
  CStringTokenizer tk(path, path + strlen(path), ":");
  // Search $PATH looking for the commands in order.
  while (tk.GetNext()) {
    for (size_t i = 0; i < ncommands; i++) {
      bin_path = FilePath(tk.token()).Append(commands[i].argv[0]);
      if (file_util::PathExists(bin_path)) {
        commands[i].binary = bin_path.value();
        if (index)
          *index = i;
        return true;
      }
    }
  }
  // Did not find any of the binaries in $PATH.
  return false;
}

// static
void NetworkSection::StartProxyConfigUtil(const ProxyConfigCommand& command) {
  std::vector<std::string> argv;
  argv.push_back(command.binary);
  for (size_t i = 1; command.argv[i]; i++)
    argv.push_back(command.argv[i]);
  base::file_handle_mapping_vector no_files;
  base::ProcessHandle handle;
  if (!base::LaunchApp(argv, no_files, false, &handle)) {
    LOG(ERROR) << "StartProxyConfigUtil failed to start " << command.binary;
    BrowserList::GetLastActive()->
        OpenURL(GURL(kLinuxProxyConfigUrl), GURL(), NEW_FOREGROUND_TAB,
                PageTransition::LINK);
    return;
  }
  ProcessWatcher::EnsureProcessGetsReaped(handle);
}

///////////////////////////////////////////////////////////////////////////////
// TranslateSection

class TranslateSection : public OptionsPageBase {
 public:
  explicit TranslateSection(Profile* profile);
  virtual ~TranslateSection() {}

  GtkWidget* get_page_widget() const {
    return page_;
  }

 private:
  // Overridden from OptionsPageBase.
  virtual void NotifyPrefChanged(const std::string* pref_name);

  CHROMEGTK_CALLBACK_0(TranslateSection, void, OnTranslateClicked);

  // Preferences for this section:
  BooleanPrefMember enable_translate_;

  // The widget containing the options for this section.
  GtkWidget* page_;

  // The checkbox.
  GtkWidget* translate_checkbox_;

  // Flag to ignore gtk callbacks while we are loading prefs, to avoid
  // then turning around and saving them again.
  bool pref_changing_;

  scoped_ptr<AccessibleWidgetHelper> accessible_widget_helper_;

  DISALLOW_COPY_AND_ASSIGN(TranslateSection);
};

TranslateSection::TranslateSection(Profile* profile)
    : OptionsPageBase(profile),
      pref_changing_(true) {
  page_ = gtk_vbox_new(FALSE, gtk_util::kControlSpacing);

  accessible_widget_helper_.reset(new AccessibleWidgetHelper(
      page_, profile));

  translate_checkbox_ = CreateCheckButtonWithWrappedLabel(
      IDS_OPTIONS_TRANSLATE_ENABLE_TRANSLATE);
  gtk_box_pack_start(GTK_BOX(page_), translate_checkbox_,
                     FALSE, FALSE, 0);
  g_signal_connect(translate_checkbox_, "clicked",
                   G_CALLBACK(OnTranslateClickedThunk), this);
  accessible_widget_helper_->SetWidgetName(
      translate_checkbox_,
      IDS_OPTIONS_TRANSLATE_ENABLE_TRANSLATE);

  // Init member prefs so we can update the controls if prefs change.
  enable_translate_.Init(prefs::kEnableTranslate, profile->GetPrefs(), this);

  NotifyPrefChanged(NULL);
}

void TranslateSection::NotifyPrefChanged(const std::string* pref_name) {
  pref_changing_ = true;
  if (!pref_name || *pref_name == prefs::kEnableTranslate) {
    gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(translate_checkbox_), enable_translate_.GetValue());
  }
  pref_changing_ = false;
}

void TranslateSection::OnTranslateClicked(GtkWidget* widget) {
  if (pref_changing_)
    return;
  bool enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  UserMetricsRecordAction(
      enabled ?
      UserMetricsAction("Options_Translate_Enable") :
      UserMetricsAction("Options_Translate_Disable"),
      profile()->GetPrefs());
  enable_translate_.SetValue(enabled);
}

///////////////////////////////////////////////////////////////////////////////
// ChromeAppsSection

class ChromeAppsSection : public OptionsPageBase {
 public:
  explicit ChromeAppsSection(Profile* profile);
  virtual ~ChromeAppsSection() {}

  GtkWidget* get_page_widget() const {
    return page_;
  }

 private:
  // Overridden from OptionsPageBase.
  virtual void NotifyPrefChanged(const std::string* pref_name);

  CHROMEGTK_CALLBACK_0(ChromeAppsSection, void, OnBackgroundModeClicked);
  CHROMEGTK_CALLBACK_0(ChromeAppsSection, void, OnLearnMoreLinkClicked);

  // Preferences for this section:
  BooleanPrefMember enable_background_mode_;

  // The widget containing the options for this section.
  GtkWidget* page_;

  // The checkbox.
  GtkWidget* background_mode_checkbox_;

  // Flag to ignore gtk callbacks while we are loading prefs, to avoid
  // then turning around and saving them again.
  bool pref_changing_;

  scoped_ptr<AccessibleWidgetHelper> accessible_widget_helper_;

  DISALLOW_COPY_AND_ASSIGN(ChromeAppsSection);
};

ChromeAppsSection::ChromeAppsSection(Profile* profile)
    : OptionsPageBase(profile),
      pref_changing_(true) {
  page_ = gtk_vbox_new(FALSE, gtk_util::kControlSpacing);

  accessible_widget_helper_.reset(new AccessibleWidgetHelper(page_, profile));

  background_mode_checkbox_ = CreateCheckButtonWithWrappedLabel(
      IDS_OPTIONS_CHROME_APPS_ENABLE_BACKGROUND_MODE);
  gtk_box_pack_start(GTK_BOX(page_), background_mode_checkbox_,
                     FALSE, FALSE, 0);
  g_signal_connect(background_mode_checkbox_, "clicked",
                   G_CALLBACK(OnBackgroundModeClickedThunk), this);
  accessible_widget_helper_->SetWidgetName(
      background_mode_checkbox_,
      IDS_OPTIONS_CHROME_APPS_ENABLE_BACKGROUND_MODE);

  // Init member prefs so we can update the controls if prefs change.
  enable_background_mode_.Init(prefs::kBackgroundModeEnabled,
                               profile->GetPrefs(), this);

  GtkWidget* learn_more_link = gtk_chrome_link_button_new(
      l10n_util::GetStringUTF8(IDS_LEARN_MORE).c_str());
  // Stick it in an hbox so it doesn't expand to the whole width.
  GtkWidget* learn_more_hbox = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(learn_more_hbox), learn_more_link,
                     FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(page_), learn_more_hbox,
                     FALSE, FALSE, 0);
  g_signal_connect(learn_more_link, "clicked",
                   G_CALLBACK(OnLearnMoreLinkClickedThunk), this);

  NotifyPrefChanged(NULL);
}

void ChromeAppsSection::NotifyPrefChanged(const std::string* pref_name) {
  pref_changing_ = true;
  if (!pref_name || *pref_name == prefs::kBackgroundModeEnabled) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(background_mode_checkbox_),
                                 enable_background_mode_.GetValue());
  }
  pref_changing_ = false;
}

void ChromeAppsSection::OnBackgroundModeClicked(GtkWidget* widget) {
  if (pref_changing_)
    return;
  bool enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  UserMetricsRecordAction(
      enabled ?
      UserMetricsAction("Options_BackgroundMode_Enable") :
      UserMetricsAction("Options_BackgroundMode_Disable"),
      profile()->GetPrefs());
  enable_background_mode_.SetValue(enabled);
}

void ChromeAppsSection::OnLearnMoreLinkClicked(GtkWidget* widget) {
  BrowserList::GetLastActive()->OpenURL(
      GURL(l10n_util::GetStringUTF8(IDS_LEARN_MORE_BACKGROUND_MODE_URL)),
      GURL(), NEW_WINDOW, PageTransition::LINK);
}


///////////////////////////////////////////////////////////////////////////////
// PrivacySection

class PrivacySection : public OptionsPageBase {
 public:
  explicit PrivacySection(Profile* profile);
  virtual ~PrivacySection() {}

  GtkWidget* get_page_widget() const {
    return page_;
  }

 private:
  // Overridden from OptionsPageBase.
  virtual void NotifyPrefChanged(const std::string* pref_name);

  // Try to make the the crash stats consent and the metrics upload
  // permission match the |reporting_enabled_checkbox_|.
  void ResolveMetricsReportingEnabled();

  // Inform the user that the browser must be restarted for changes to take
  // effect.
  void ShowRestartMessageBox() const;

  // The callback functions for the options widgets.
  static void OnContentSettingsClicked(GtkButton* button,
                                       PrivacySection* privacy_section);
  static void OnClearBrowsingDataButtonClicked(GtkButton* widget,
                                               PrivacySection* page);
  static void OnLearnMoreLinkClicked(GtkButton *button,
                                     PrivacySection* privacy_section);
  static void OnEnableLinkDoctorChange(GtkWidget* widget,
                                       PrivacySection* options_window);
  static void OnEnableSuggestChange(GtkWidget* widget,
                                    PrivacySection* options_window);
  static void OnDNSPrefetchingChange(GtkWidget* widget,
                                     PrivacySection* options_window);
  static void OnSafeBrowsingChange(GtkWidget* widget,
                                   PrivacySection* options_window);
  static void OnLoggingChange(GtkWidget* widget,
                              PrivacySection* options_window);

  // The widget containing the options for this section.
  GtkWidget* page_;

  // The widgets for the privacy options.
  GtkWidget* enable_link_doctor_checkbox_;
  GtkWidget* enable_suggest_checkbox_;
  GtkWidget* enable_dns_prefetching_checkbox_;
  GtkWidget* enable_safe_browsing_checkbox_;
#if defined(GOOGLE_CHROME_BUILD)
  GtkWidget* reporting_enabled_checkbox_;
#endif

  // Preferences for this section:
  BooleanPrefMember alternate_error_pages_;
  BooleanPrefMember use_suggest_;
  BooleanPrefMember dns_prefetch_enabled_;
  BooleanPrefMember safe_browsing_;
  BooleanPrefMember enable_metrics_recording_;

  // Flag to ignore gtk callbacks while we are loading prefs, to avoid
  // then turning around and saving them again.
  bool pref_changing_;

  scoped_ptr<AccessibleWidgetHelper> accessible_widget_helper_;

  DISALLOW_COPY_AND_ASSIGN(PrivacySection);
};

PrivacySection::PrivacySection(Profile* profile)
    : OptionsPageBase(profile),
      pref_changing_(true) {
  page_ = gtk_vbox_new(FALSE, gtk_util::kControlSpacing);

  accessible_widget_helper_.reset(new AccessibleWidgetHelper(
      page_, profile));

  GtkWidget* content_button = gtk_button_new_with_label(
      l10n_util::GetStringUTF8(
          IDS_OPTIONS_PRIVACY_CONTENT_SETTINGS_BUTTON).c_str());
  g_signal_connect(content_button, "clicked",
                   G_CALLBACK(OnContentSettingsClicked), this);

  GtkWidget* clear_data_button = gtk_button_new_with_label(
      l10n_util::GetStringUTF8(IDS_OPTIONS_PRIVACY_CLEAR_DATA_BUTTON).c_str());
  g_signal_connect(clear_data_button, "clicked",
                   G_CALLBACK(OnClearBrowsingDataButtonClicked), this);

  // Stick it in an hbox so it doesn't expand to the whole width.
  GtkWidget* button_hbox = gtk_hbox_new(FALSE, gtk_util::kControlSpacing);
  gtk_box_pack_start(GTK_BOX(button_hbox), content_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(button_hbox), clear_data_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(page_), gtk_util::IndentWidget(button_hbox),
                     FALSE, FALSE, 0);

  GtkWidget* section_description_label = CreateWrappedLabel(
      IDS_OPTIONS_DISABLE_SERVICES);
  gtk_misc_set_alignment(GTK_MISC(section_description_label), 0, 0);
  gtk_box_pack_start(GTK_BOX(page_), section_description_label,
                     FALSE, FALSE, 0);

  GtkWidget* learn_more_link = gtk_chrome_link_button_new(
      l10n_util::GetStringUTF8(IDS_LEARN_MORE).c_str());
  // Stick it in an hbox so it doesn't expand to the whole width.
  GtkWidget* learn_more_hbox = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(learn_more_hbox), learn_more_link,
                     FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(page_), learn_more_hbox,
                     FALSE, FALSE, 0);
  g_signal_connect(learn_more_link, "clicked",
                   G_CALLBACK(OnLearnMoreLinkClicked), this);

  enable_link_doctor_checkbox_ = CreateCheckButtonWithWrappedLabel(
      IDS_OPTIONS_LINKDOCTOR_PREF);
  gtk_box_pack_start(GTK_BOX(page_), enable_link_doctor_checkbox_,
                     FALSE, FALSE, 0);
  g_signal_connect(enable_link_doctor_checkbox_, "clicked",
                   G_CALLBACK(OnEnableLinkDoctorChange), this);
  accessible_widget_helper_->SetWidgetName(
      enable_link_doctor_checkbox_, IDS_OPTIONS_LINKDOCTOR_PREF);

  enable_suggest_checkbox_ = CreateCheckButtonWithWrappedLabel(
      IDS_OPTIONS_SUGGEST_PREF);
  gtk_box_pack_start(GTK_BOX(page_), enable_suggest_checkbox_,
                     FALSE, FALSE, 0);
  g_signal_connect(enable_suggest_checkbox_, "clicked",
                   G_CALLBACK(OnEnableSuggestChange), this);
  accessible_widget_helper_->SetWidgetName(
      enable_suggest_checkbox_, IDS_OPTIONS_SUGGEST_PREF);

  enable_dns_prefetching_checkbox_ = CreateCheckButtonWithWrappedLabel(
      IDS_NETWORK_DNS_PREFETCH_ENABLED_DESCRIPTION);
  gtk_box_pack_start(GTK_BOX(page_), enable_dns_prefetching_checkbox_,
                     FALSE, FALSE, 0);
  g_signal_connect(enable_dns_prefetching_checkbox_, "clicked",
                   G_CALLBACK(OnDNSPrefetchingChange), this);
  accessible_widget_helper_->SetWidgetName(
      enable_dns_prefetching_checkbox_,
      IDS_NETWORK_DNS_PREFETCH_ENABLED_DESCRIPTION);

  enable_safe_browsing_checkbox_ = CreateCheckButtonWithWrappedLabel(
      IDS_OPTIONS_SAFEBROWSING_ENABLEPROTECTION);
  gtk_box_pack_start(GTK_BOX(page_), enable_safe_browsing_checkbox_,
                     FALSE, FALSE, 0);
  g_signal_connect(enable_safe_browsing_checkbox_, "clicked",
                   G_CALLBACK(OnSafeBrowsingChange), this);
  accessible_widget_helper_->SetWidgetName(
      enable_safe_browsing_checkbox_,
      IDS_OPTIONS_SAFEBROWSING_ENABLEPROTECTION);

#if defined(GOOGLE_CHROME_BUILD)
  reporting_enabled_checkbox_ = CreateCheckButtonWithWrappedLabel(
      IDS_OPTIONS_ENABLE_LOGGING);
  gtk_box_pack_start(GTK_BOX(page_), reporting_enabled_checkbox_,
                     FALSE, FALSE, 0);
  g_signal_connect(reporting_enabled_checkbox_, "clicked",
                   G_CALLBACK(OnLoggingChange), this);
  accessible_widget_helper_->SetWidgetName(
      reporting_enabled_checkbox_, IDS_OPTIONS_ENABLE_LOGGING);
#endif

  // Init member prefs so we can update the controls if prefs change.
  alternate_error_pages_.Init(prefs::kAlternateErrorPagesEnabled,
                              profile->GetPrefs(), this);
  use_suggest_.Init(prefs::kSearchSuggestEnabled,
                    profile->GetPrefs(), this);
  dns_prefetch_enabled_.Init(prefs::kDnsPrefetchingEnabled,
                             profile->GetPrefs(), this);
  safe_browsing_.Init(prefs::kSafeBrowsingEnabled, profile->GetPrefs(), this);
  enable_metrics_recording_.Init(prefs::kMetricsReportingEnabled,
                                 g_browser_process->local_state(), this);
  NotifyPrefChanged(NULL);
}

// static
void PrivacySection::OnContentSettingsClicked(GtkButton* button,
                                              PrivacySection* privacy_section) {
  ContentSettingsWindowGtk::Show(
      GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button))),
      CONTENT_SETTINGS_TYPE_DEFAULT,
      privacy_section->profile());
}

// static
void PrivacySection::OnClearBrowsingDataButtonClicked(GtkButton* widget,
                                                      PrivacySection* section) {
  ClearBrowsingDataDialogGtk::Show(
      GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(widget))),
      section->profile());
}

// static
void PrivacySection::OnLearnMoreLinkClicked(GtkButton *button,
                                            PrivacySection* privacy_section) {
  BrowserList::GetLastActive()->
      OpenURL(GURL(l10n_util::GetStringUTF8(IDS_LEARN_MORE_PRIVACY_URL)),
              GURL(), NEW_WINDOW, PageTransition::LINK);
}

// static
void PrivacySection::OnEnableLinkDoctorChange(GtkWidget* widget,
                                              PrivacySection* privacy_section) {
  if (privacy_section->pref_changing_)
    return;
  bool enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  privacy_section->UserMetricsRecordAction(
      enabled ?
          UserMetricsAction("Options_LinkDoctorCheckbox_Enable") :
          UserMetricsAction("Options_LinkDoctorCheckbox_Disable"),
      privacy_section->profile()->GetPrefs());
  privacy_section->alternate_error_pages_.SetValue(enabled);
}

// static
void PrivacySection::OnEnableSuggestChange(GtkWidget* widget,
                                           PrivacySection* privacy_section) {
  if (privacy_section->pref_changing_)
    return;
  bool enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  privacy_section->UserMetricsRecordAction(
      enabled ?
          UserMetricsAction("Options_UseSuggestCheckbox_Enable") :
          UserMetricsAction("Options_UseSuggestCheckbox_Disable"),
      privacy_section->profile()->GetPrefs());
  privacy_section->use_suggest_.SetValue(enabled);
}

// static
void PrivacySection::OnDNSPrefetchingChange(GtkWidget* widget,
                                           PrivacySection* privacy_section) {
  if (privacy_section->pref_changing_)
    return;
  bool enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  privacy_section->UserMetricsRecordAction(
      enabled ?
          UserMetricsAction("Options_DnsPrefetchCheckbox_Enable") :
          UserMetricsAction("Options_DnsPrefetchCheckbox_Disable"),
      privacy_section->profile()->GetPrefs());
  privacy_section->dns_prefetch_enabled_.SetValue(enabled);
  chrome_browser_net::EnablePredictor(enabled);
}

// static
void PrivacySection::OnSafeBrowsingChange(GtkWidget* widget,
                                          PrivacySection* privacy_section) {
  if (privacy_section->pref_changing_)
    return;
  bool enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  privacy_section->UserMetricsRecordAction(
      enabled ?
          UserMetricsAction("Options_SafeBrowsingCheckbox_Enable") :
          UserMetricsAction("Options_SafeBrowsingCheckbox_Disable"),
      privacy_section->profile()->GetPrefs());
  privacy_section->safe_browsing_.SetValue(enabled);
  SafeBrowsingService* safe_browsing_service =
      g_browser_process->resource_dispatcher_host()->safe_browsing_service();
  MessageLoop::current()->PostTask(FROM_HERE, NewRunnableMethod(
      safe_browsing_service, &SafeBrowsingService::OnEnable, enabled));
}

// static
void PrivacySection::OnLoggingChange(GtkWidget* widget,
                                     PrivacySection* privacy_section) {
  if (privacy_section->pref_changing_)
    return;
  bool enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  privacy_section->UserMetricsRecordAction(
      enabled ?
          UserMetricsAction("Options_MetricsReportingCheckbox_Enable") :
          UserMetricsAction("Options_MetricsReportingCheckbox_Disable"),
      privacy_section->profile()->GetPrefs());
  // Prevent us from being called again by ResolveMetricsReportingEnabled
  // resetting the checkbox if there was a problem.
  g_signal_handlers_block_by_func(widget,
                                  reinterpret_cast<gpointer>(OnLoggingChange),
                                  privacy_section);
  privacy_section->ResolveMetricsReportingEnabled();
  if (enabled == gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
    privacy_section->ShowRestartMessageBox();
  g_signal_handlers_unblock_by_func(widget,
                                    reinterpret_cast<gpointer>(OnLoggingChange),
                                    privacy_section);
  privacy_section->enable_metrics_recording_.SetValue(enabled);
}

void PrivacySection::NotifyPrefChanged(const std::string* pref_name) {
  pref_changing_ = true;
  if (!pref_name || *pref_name == prefs::kAlternateErrorPagesEnabled) {
    gtk_widget_set_sensitive(
        GTK_WIDGET(enable_link_doctor_checkbox_),
        !alternate_error_pages_.IsManaged());
    gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(enable_link_doctor_checkbox_),
        alternate_error_pages_.GetValue());
  }
  if (!pref_name || *pref_name == prefs::kSearchSuggestEnabled) {
    gtk_widget_set_sensitive(
        GTK_WIDGET(enable_suggest_checkbox_),
        !use_suggest_.IsManaged());
    gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(enable_suggest_checkbox_),
        use_suggest_.GetValue());
  }
  if (!pref_name || *pref_name == prefs::kDnsPrefetchingEnabled) {
    gtk_widget_set_sensitive(
        GTK_WIDGET(enable_dns_prefetching_checkbox_),
        !dns_prefetch_enabled_.IsManaged());
    bool enabled = dns_prefetch_enabled_.GetValue();
    gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(enable_dns_prefetching_checkbox_), enabled);
    chrome_browser_net::EnablePredictor(enabled);
  }
  if (!pref_name || *pref_name == prefs::kSafeBrowsingEnabled) {
    gtk_widget_set_sensitive(
        GTK_WIDGET(enable_safe_browsing_checkbox_),
        !safe_browsing_.IsManaged());
    gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(enable_safe_browsing_checkbox_),
        safe_browsing_.GetValue());
  }
#if defined(GOOGLE_CHROME_BUILD)
  if (!pref_name || *pref_name == prefs::kMetricsReportingEnabled) {
    gtk_widget_set_sensitive(
        GTK_WIDGET(reporting_enabled_checkbox_),
        !enable_metrics_recording_.IsManaged());
    gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(reporting_enabled_checkbox_),
        enable_metrics_recording_.GetValue());
    ResolveMetricsReportingEnabled();
  }
#endif
  pref_changing_ = false;
}

void PrivacySection::ResolveMetricsReportingEnabled() {
#if defined(GOOGLE_CHROME_BUILD)
  bool enabled = gtk_toggle_button_get_active(
      GTK_TOGGLE_BUTTON(reporting_enabled_checkbox_));

  enabled = OptionsUtil::ResolveMetricsReportingEnabled(enabled);

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(reporting_enabled_checkbox_),
                               enabled);
#endif
}

void PrivacySection::ShowRestartMessageBox() const {
  GtkWidget* dialog = gtk_message_dialog_new(
      GTK_WINDOW(gtk_widget_get_toplevel(page_)),
      static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL),
      GTK_MESSAGE_INFO,
      GTK_BUTTONS_OK,
      "%s",
      l10n_util::GetStringUTF8(IDS_OPTIONS_RESTART_REQUIRED).c_str());
  gtk_util::ApplyMessageDialogQuirks(dialog);
  gtk_window_set_title(GTK_WINDOW(dialog),
      l10n_util::GetStringUTF8(IDS_PRODUCT_NAME).c_str());
  g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy),
                           dialog);
  gtk_util::ShowDialog(dialog);
}

///////////////////////////////////////////////////////////////////////////////
// SecuritySection

class SecuritySection : public OptionsPageBase {
 public:
  explicit SecuritySection(Profile* profile);
  virtual ~SecuritySection() {}

  GtkWidget* get_page_widget() const {
    return page_;
  }

 private:
  // Overridden from OptionsPageBase.
  virtual void NotifyPrefChanged(const std::string* pref_name);

  // The callback functions for the options widgets.
  static void OnManageCertificatesClicked(GtkButton* button,
                                          SecuritySection* section);
  static void OnRevCheckingEnabledToggled(GtkToggleButton* togglebutton,
                                          SecuritySection* section);
  static void OnSSL2EnabledToggled(GtkToggleButton* togglebutton,
                                   SecuritySection* section);
  static void OnSSL3EnabledToggled(GtkToggleButton* togglebutton,
                                   SecuritySection* section);
  static void OnTLS1EnabledToggled(GtkToggleButton* togglebutton,
                                   SecuritySection* section);

  // The widget containing the options for this section.
  GtkWidget* page_;
  GtkWidget* rev_checking_enabled_checkbox_;
  GtkWidget* ssl2_enabled_checkbox_;
  GtkWidget* ssl3_enabled_checkbox_;
  GtkWidget* tls1_enabled_checkbox_;

  // SSLConfigService prefs.
  BooleanPrefMember rev_checking_enabled_;
  BooleanPrefMember ssl2_enabled_;
  BooleanPrefMember ssl3_enabled_;
  BooleanPrefMember tls1_enabled_;

  // Flag to ignore gtk callbacks while we are loading prefs, to avoid
  // then turning around and saving them again.
  bool pref_changing_;

  scoped_ptr<AccessibleWidgetHelper> accessible_widget_helper_;

  DISALLOW_COPY_AND_ASSIGN(SecuritySection);
};

SecuritySection::SecuritySection(Profile* profile)
    : OptionsPageBase(profile),
      pref_changing_(true) {
  page_ = gtk_vbox_new(FALSE, gtk_util::kControlSpacing);

  accessible_widget_helper_.reset(new AccessibleWidgetHelper(
      page_, profile));

  GtkWidget* manage_certificates_label = CreateWrappedLabel(
      IDS_OPTIONS_CERTIFICATES_LABEL);
  gtk_misc_set_alignment(GTK_MISC(manage_certificates_label), 0, 0);
  gtk_box_pack_start(GTK_BOX(page_), manage_certificates_label,
                     FALSE, FALSE, 0);

  // TODO(mattm): change this to a button to launch the system certificate
  // manager, when one exists.
  GtkWidget* manage_certificates_link = gtk_chrome_link_button_new(
      l10n_util::GetStringUTF8(IDS_OPTIONS_CERTIFICATES_MANAGE_BUTTON).c_str());
  // Stick it in an hbox so it doesn't expand to the whole width.
  GtkWidget* manage_certificates_hbox = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(manage_certificates_hbox),
                     manage_certificates_link, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(page_),
                     gtk_util::IndentWidget(manage_certificates_hbox),
                     FALSE, FALSE, 0);
  g_signal_connect(manage_certificates_link, "clicked",
                   G_CALLBACK(OnManageCertificatesClicked), this);

  // TODO(mattm): should have a description label here and have the checkboxes
  // indented, but IDS_OPTIONS_SSL_GROUP_DESCRIPTION isn't appropriate and
  // didn't think of adding a Linux specific one before the string freeze.
  rev_checking_enabled_checkbox_ = AddCheckButtonWithWrappedLabel(
      IDS_OPTIONS_SSL_CHECKREVOCATION, page_,
      G_CALLBACK(OnRevCheckingEnabledToggled), this);
  accessible_widget_helper_->SetWidgetName(
      rev_checking_enabled_checkbox_, IDS_OPTIONS_SSL_CHECKREVOCATION);
  ssl2_enabled_checkbox_ = AddCheckButtonWithWrappedLabel(
      IDS_OPTIONS_SSL_USESSL2, page_, G_CALLBACK(OnSSL2EnabledToggled), this);
  accessible_widget_helper_->SetWidgetName(
      ssl2_enabled_checkbox_, IDS_OPTIONS_SSL_USESSL2);
  ssl3_enabled_checkbox_ = AddCheckButtonWithWrappedLabel(
      IDS_OPTIONS_SSL_USESSL3, page_, G_CALLBACK(OnSSL3EnabledToggled), this);
  accessible_widget_helper_->SetWidgetName(
      ssl3_enabled_checkbox_, IDS_OPTIONS_SSL_USESSL3);
  tls1_enabled_checkbox_ = AddCheckButtonWithWrappedLabel(
      IDS_OPTIONS_SSL_USETLS1, page_, G_CALLBACK(OnTLS1EnabledToggled), this);
  accessible_widget_helper_->SetWidgetName(
      tls1_enabled_checkbox_, IDS_OPTIONS_SSL_USETLS1);

  rev_checking_enabled_.Init(prefs::kCertRevocationCheckingEnabled,
                             profile->GetPrefs(), this);
  ssl2_enabled_.Init(prefs::kSSL2Enabled, profile->GetPrefs(), this);
  ssl3_enabled_.Init(prefs::kSSL3Enabled, profile->GetPrefs(), this);
  tls1_enabled_.Init(prefs::kTLS1Enabled, profile->GetPrefs(), this);

  NotifyPrefChanged(NULL);
}

void SecuritySection::NotifyPrefChanged(const std::string* pref_name) {
  pref_changing_ = true;
  if (!pref_name || *pref_name == prefs::kCertRevocationCheckingEnabled) {
    gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(rev_checking_enabled_checkbox_),
        rev_checking_enabled_.GetValue());
  }
  if (!pref_name || *pref_name == prefs::kSSL2Enabled) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ssl2_enabled_checkbox_),
                                 ssl2_enabled_.GetValue());
  }
  if (!pref_name || *pref_name == prefs::kSSL3Enabled) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ssl3_enabled_checkbox_),
                                 ssl3_enabled_.GetValue());
  }
  if (!pref_name || *pref_name == prefs::kTLS1Enabled) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tls1_enabled_checkbox_),
                                 tls1_enabled_.GetValue());
  }
  pref_changing_ = false;
}


// static
void SecuritySection::OnManageCertificatesClicked(GtkButton* button,
                                                  SecuritySection* section) {
  BrowserList::GetLastActive()->
      OpenURL(GURL(kLinuxCertificatesConfigUrl), GURL(), NEW_WINDOW,
              PageTransition::LINK);
}

// static
void SecuritySection::OnRevCheckingEnabledToggled(GtkToggleButton* togglebutton,
                                                  SecuritySection* section) {
  if (section->pref_changing_)
    return;

  bool enabled = gtk_toggle_button_get_active(togglebutton);
  if (enabled) {
    section->UserMetricsRecordAction(
        UserMetricsAction("Options_CheckCertRevocation_Enable"),
        NULL);
  } else {
    section->UserMetricsRecordAction(
        UserMetricsAction("Options_CheckCertRevocation_Disable"),
        NULL);
  }
  section->rev_checking_enabled_.SetValue(enabled);
}

// static
void SecuritySection::OnSSL2EnabledToggled(GtkToggleButton* togglebutton,
                                           SecuritySection* section) {
  if (section->pref_changing_)
    return;

  bool enabled = gtk_toggle_button_get_active(togglebutton);
  if (enabled) {
    section->UserMetricsRecordAction(UserMetricsAction("Options_SSL2_Enable"),
                                     NULL);
  } else {
    section->UserMetricsRecordAction(UserMetricsAction("Options_SSL2_Disable"),
                                     NULL);
  }
  section->ssl2_enabled_.SetValue(enabled);
}

// static
void SecuritySection::OnSSL3EnabledToggled(GtkToggleButton* togglebutton,
                                           SecuritySection* section) {
  if (section->pref_changing_)
    return;

  bool enabled = gtk_toggle_button_get_active(togglebutton);
  if (enabled) {
    section->UserMetricsRecordAction(UserMetricsAction("Options_SSL3_Enable"),
                                     NULL);
  } else {
    section->UserMetricsRecordAction(UserMetricsAction("Options_SSL3_Disable"),
                                     NULL);
  }
  section->ssl3_enabled_.SetValue(enabled);
}

// static
void SecuritySection::OnTLS1EnabledToggled(GtkToggleButton* togglebutton,
                                           SecuritySection* section) {
  if (section->pref_changing_)
    return;

  bool enabled = gtk_toggle_button_get_active(togglebutton);
  if (enabled) {
    section->UserMetricsRecordAction(UserMetricsAction("Options_TLS1_Enable"),
                                     NULL);
  } else {
    section->UserMetricsRecordAction(UserMetricsAction("Options_TLS1_Disable"),
                                     NULL);
  }
  section->tls1_enabled_.SetValue(enabled);
}

///////////////////////////////////////////////////////////////////////////////
// WebContentSection

class WebContentSection : public OptionsPageBase {
 public:
  explicit WebContentSection(Profile* profile);
  virtual ~WebContentSection() {}

  GtkWidget* get_page_widget() const {
    return page_;
  }

 private:
  // The callback functions for the options widgets.
  static void OnFontsAndLanguagesButtonClicked(GtkButton *button,
                                               WebContentSection* section);

  // The widget containing the options for this section.
  GtkWidget* page_;

  DISALLOW_COPY_AND_ASSIGN(WebContentSection);
};

WebContentSection::WebContentSection(Profile* profile)
    : OptionsPageBase(profile) {
  page_ = gtk_vbox_new(FALSE, gtk_util::kControlSpacing);

  GtkWidget* fonts_and_languages_label = CreateWrappedLabel(
      IDS_OPTIONS_FONTSETTINGS_INFO);
  gtk_misc_set_alignment(GTK_MISC(fonts_and_languages_label), 0, 0);
  gtk_box_pack_start(GTK_BOX(page_), fonts_and_languages_label,
                     FALSE, FALSE, 0);

  GtkWidget* fonts_and_languages_button = gtk_button_new_with_label(
      l10n_util::GetStringUTF8(
          IDS_OPTIONS_FONTSETTINGS_CONFIGUREFONTS_BUTTON).c_str());
  g_signal_connect(fonts_and_languages_button, "clicked",
                   G_CALLBACK(OnFontsAndLanguagesButtonClicked), this);
  // Stick it in an hbox so it doesn't expand to the whole width.
  GtkWidget* button_hbox = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(button_hbox),
                     fonts_and_languages_button,
                     FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(page_),
                     gtk_util::IndentWidget(button_hbox),
                     FALSE, FALSE, 0);

  // TODO(mattm): gears options would go here if we supported gears
}

// static
void WebContentSection::OnFontsAndLanguagesButtonClicked(
    GtkButton *button, WebContentSection* section) {
  ShowFontsLanguagesWindow(GTK_WINDOW(gtk_widget_get_toplevel(section->page_)),
                           FONTS_ENCODING_PAGE,
                           section->profile());
}

///////////////////////////////////////////////////////////////////////////////
// AdvancedContentsGtk

AdvancedContentsGtk::AdvancedContentsGtk(Profile* profile)
    : profile_(profile) {
  Init();
}

AdvancedContentsGtk::~AdvancedContentsGtk() {
}

void AdvancedContentsGtk::Init() {
  scoped_ptr<OptionsLayoutBuilderGtk>
    options_builder(OptionsLayoutBuilderGtk::Create());

  privacy_section_.reset(new PrivacySection(profile_));
  options_builder->AddOptionGroup(
      l10n_util::GetStringUTF8(IDS_OPTIONS_ADVANCED_SECTION_TITLE_PRIVACY),
      privacy_section_->get_page_widget(), false);

  network_section_.reset(new NetworkSection(profile_));
  options_builder->AddOptionGroup(
      l10n_util::GetStringUTF8(IDS_OPTIONS_ADVANCED_SECTION_TITLE_NETWORK),
      network_section_->get_page_widget(), false);

  translate_section_.reset(new TranslateSection(profile_));
  options_builder->AddOptionGroup(
      l10n_util::GetStringUTF8(IDS_OPTIONS_ADVANCED_SECTION_TITLE_TRANSLATE),
      translate_section_->get_page_widget(), false);

  download_section_.reset(new DownloadSection(profile_));
  options_builder->AddOptionGroup(
      l10n_util::GetStringUTF8(IDS_OPTIONS_DOWNLOADLOCATION_GROUP_NAME),
      download_section_->get_page_widget(), false);

  web_content_section_.reset(new WebContentSection(profile_));
  options_builder->AddOptionGroup(
      l10n_util::GetStringUTF8(IDS_OPTIONS_ADVANCED_SECTION_TITLE_CONTENT),
      web_content_section_->get_page_widget(), false);

  security_section_.reset(new SecuritySection(profile_));
  options_builder->AddOptionGroup(
      l10n_util::GetStringUTF8(IDS_OPTIONS_ADVANCED_SECTION_TITLE_SECURITY),
      security_section_->get_page_widget(), false);

  // Add ChromeApps preferences if background mode is runtime-enabled.
  if (CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableBackgroundMode)) {
    chrome_apps_section_.reset(new ChromeAppsSection(profile_));
    options_builder->AddOptionGroup(l10n_util::GetStringUTF8(
        IDS_OPTIONS_ADVANCED_SECTION_TITLE_CHROME_APPS),
        chrome_apps_section_->get_page_widget(), false);
  }
  page_ = options_builder->get_page_widget();
}
