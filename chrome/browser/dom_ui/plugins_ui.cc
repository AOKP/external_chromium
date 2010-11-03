// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/dom_ui/plugins_ui.h"

#include <algorithm>
#include <string>
#include <vector>

#include "app/l10n_util.h"
#include "app/resource_bundle.h"
#include "base/message_loop.h"
#include "base/path_service.h"
#include "base/singleton.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser.h"
#include "chrome/browser/browser_thread.h"
#include "chrome/browser/browser_window.h"
#include "chrome/browser/dom_ui/chrome_url_data_manager.h"
#include "chrome/browser/plugin_updater.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/jstemplate_builder.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/pepper_plugin_registry.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "grit/browser_resources.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "webkit/glue/plugins/plugin_list.h"

namespace {

///////////////////////////////////////////////////////////////////////////////
//
// PluginsHTMLSource
//
///////////////////////////////////////////////////////////////////////////////

class PluginsUIHTMLSource : public ChromeURLDataManager::DataSource {
 public:
  PluginsUIHTMLSource()
      : DataSource(chrome::kChromeUIPluginsHost, MessageLoop::current()) {}

  // Called when the network layer has requested a resource underneath
  // the path we registered.
  virtual void StartDataRequest(const std::string& path,
                                bool is_off_the_record,
                                int request_id);
  virtual std::string GetMimeType(const std::string&) const {
    return "text/html";
  }

 private:
  ~PluginsUIHTMLSource() {}

  DISALLOW_COPY_AND_ASSIGN(PluginsUIHTMLSource);
};

void PluginsUIHTMLSource::StartDataRequest(const std::string& path,
                                           bool is_off_the_record,
                                           int request_id) {
  // Strings used in the JsTemplate file.
  DictionaryValue localized_strings;
  localized_strings.SetString("pluginsTitle",
      l10n_util::GetStringUTF16(IDS_PLUGINS_TITLE));
  localized_strings.SetString("pluginsDetailsModeLink",
      l10n_util::GetStringUTF16(IDS_PLUGINS_DETAILS_MODE_LINK));
  localized_strings.SetString("pluginsNoneInstalled",
      l10n_util::GetStringUTF16(IDS_PLUGINS_NONE_INSTALLED));
  localized_strings.SetString("pluginDisabled",
      l10n_util::GetStringUTF16(IDS_PLUGINS_DISABLED_PLUGIN));
  localized_strings.SetString("pluginDisabledByPolicy",
      l10n_util::GetStringUTF16(IDS_PLUGINS_DISABLED_BY_POLICY_PLUGIN));
  localized_strings.SetString("pluginCannotBeEnabledDueToPolicy",
      l10n_util::GetStringUTF16(IDS_PLUGINS_CANNOT_ENABLE_DUE_TO_POLICY));
  localized_strings.SetString("pluginDownload",
      l10n_util::GetStringUTF16(IDS_PLUGINS_DOWNLOAD));
  localized_strings.SetString("pluginName",
      l10n_util::GetStringUTF16(IDS_PLUGINS_NAME));
  localized_strings.SetString("pluginPriority",
      l10n_util::GetStringUTF16(IDS_PLUGINS_PRIORITY));
  localized_strings.SetString("pluginVersion",
      l10n_util::GetStringUTF16(IDS_PLUGINS_VERSION));
  localized_strings.SetString("pluginDescription",
      l10n_util::GetStringUTF16(IDS_PLUGINS_DESCRIPTION));
  localized_strings.SetString("pluginPath",
      l10n_util::GetStringUTF16(IDS_PLUGINS_PATH));
  localized_strings.SetString("pluginMimeTypes",
      l10n_util::GetStringUTF16(IDS_PLUGINS_MIME_TYPES));
  localized_strings.SetString("pluginMimeTypesMimeType",
      l10n_util::GetStringUTF16(IDS_PLUGINS_MIME_TYPES_MIME_TYPE));
  localized_strings.SetString("pluginMimeTypesDescription",
      l10n_util::GetStringUTF16(IDS_PLUGINS_MIME_TYPES_DESCRIPTION));
  localized_strings.SetString("pluginMimeTypesFileExtensions",
      l10n_util::GetStringUTF16(IDS_PLUGINS_MIME_TYPES_FILE_EXTENSIONS));
  localized_strings.SetString("disable",
      l10n_util::GetStringUTF16(IDS_PLUGINS_DISABLE));
  localized_strings.SetString("enable",
      l10n_util::GetStringUTF16(IDS_PLUGINS_ENABLE));

  ChromeURLDataManager::DataSource::SetFontAndTextDirection(&localized_strings);

  static const base::StringPiece plugins_html(
      ResourceBundle::GetSharedInstance().GetRawDataResource(IDR_PLUGINS_HTML));
  std::string full_html(plugins_html.data(), plugins_html.size());
  jstemplate_builder::AppendJsonHtml(&localized_strings, &full_html);
  jstemplate_builder::AppendI18nTemplateSourceHtml(&full_html);
  jstemplate_builder::AppendI18nTemplateProcessHtml(&full_html);
  jstemplate_builder::AppendJsTemplateSourceHtml(&full_html);

  scoped_refptr<RefCountedBytes> html_bytes(new RefCountedBytes);
  html_bytes->data.resize(full_html.size());
  std::copy(full_html.begin(), full_html.end(), html_bytes->data.begin());

  SendResponse(request_id, html_bytes);
}

////////////////////////////////////////////////////////////////////////////////
//
// PluginsDOMHandler
//
////////////////////////////////////////////////////////////////////////////////

// The handler for Javascript messages for the chrome://plugins/ page.
// TODO(viettrungluu): Make plugin list updates notify, and then observe
// changes; maybe replumb plugin list through plugin service?
// <http://crbug.com/39101>
class PluginsDOMHandler : public DOMMessageHandler,
                          public NotificationObserver {
 public:
  explicit PluginsDOMHandler();
  virtual ~PluginsDOMHandler() {}

  // DOMMessageHandler implementation.
  virtual void RegisterMessages();

  // Callback for the "requestPluginsData" message.
  void HandleRequestPluginsData(const ListValue* args);

  // Callback for the "enablePlugin" message.
  void HandleEnablePluginMessage(const ListValue* args);

  // Callback for the "showTermsOfService" message. This really just opens a new
  // window with about:terms. Flash can't link directly to about:terms due to
  // the security model.
  void HandleShowTermsOfServiceMessage(const ListValue* args);

  // NotificationObserver method overrides
  void Observe(NotificationType type,
               const NotificationSource& source,
               const NotificationDetails& details);

 private:
  // This extra wrapper is used to ensure we don't leak the ListValue* pointer
  // if the PluginsDOMHandler object goes away before the task on the UI thread
  // to give it the plugin list runs.
  struct ListWrapper {
    ListValue* list;
  };
  // Loads the plugins on the FILE thread.
  static void LoadPluginsOnFileThread(ListWrapper* wrapper, Task* task);

  // Used in conjunction with ListWrapper to avoid any memory leaks.
  static void EnsureListDeleted(ListWrapper* wrapper);

  // Call this to start getting the plugins on the UI thread.
  void LoadPlugins();

  // Called on the UI thread when the plugin information is ready.
  void PluginsLoaded(ListWrapper* wrapper);

  NotificationRegistrar registrar_;

  ScopedRunnableMethodFactory<PluginsDOMHandler> get_plugins_factory_;

  DISALLOW_COPY_AND_ASSIGN(PluginsDOMHandler);
};

PluginsDOMHandler::PluginsDOMHandler()
    : ALLOW_THIS_IN_INITIALIZER_LIST(get_plugins_factory_(this)) {
  registrar_.Add(this,
                 NotificationType::PLUGIN_ENABLE_STATUS_CHANGED,
                 NotificationService::AllSources());
}

void PluginsDOMHandler::RegisterMessages() {
  dom_ui_->RegisterMessageCallback("requestPluginsData",
      NewCallback(this, &PluginsDOMHandler::HandleRequestPluginsData));
  dom_ui_->RegisterMessageCallback("enablePlugin",
      NewCallback(this, &PluginsDOMHandler::HandleEnablePluginMessage));
  dom_ui_->RegisterMessageCallback("showTermsOfService",
      NewCallback(this, &PluginsDOMHandler::HandleShowTermsOfServiceMessage));
}

void PluginsDOMHandler::HandleRequestPluginsData(const ListValue* args) {
  LoadPlugins();
}

void PluginsDOMHandler::HandleEnablePluginMessage(const ListValue* args) {
  // Be robust in accepting badness since plug-ins display HTML (hence
  // JavaScript).
  if (args->GetSize() != 3)
    return;

  std::string enable_str;
  std::string is_group_str;
  if (!args->GetString(1, &enable_str) || !args->GetString(2, &is_group_str))
    return;
  bool enable = enable_str == "true";

  PluginUpdater* plugin_updater = PluginUpdater::GetPluginUpdater();
  if (is_group_str == "true") {
    string16 group_name;
    if (!args->GetString(0, &group_name))
      return;

    plugin_updater->EnablePluginGroup(enable, group_name);
    if (enable) {
      // See http://crbug.com/50105 for background.
      string16 reader8 = ASCIIToUTF16(PluginGroup::kAdobeReader8GroupName);
      string16 reader9 = ASCIIToUTF16(PluginGroup::kAdobeReader9GroupName);
      string16 internalpdf = ASCIIToUTF16(PepperPluginRegistry::kPDFPluginName);
      if (group_name == reader8 || group_name == reader9) {
        plugin_updater->EnablePluginGroup(false, internalpdf);
      } else if (group_name == internalpdf) {
        plugin_updater->EnablePluginGroup(false, reader8);
        plugin_updater->EnablePluginGroup(false, reader9);
      }
    }
  } else {
    FilePath::StringType file_path;
    if (!args->GetString(0, &file_path))
      return;

    plugin_updater->EnablePluginFile(enable, file_path);
  }

  // TODO(viettrungluu): We might also want to ensure that the plugins
  // list is always written to prefs even when the user hasn't disabled a
  // plugin. <http://crbug.com/39101>
  plugin_updater->UpdatePreferences(dom_ui_->GetProfile());
}

void PluginsDOMHandler::HandleShowTermsOfServiceMessage(const ListValue* args) {
  // Show it in a new browser window....
  Browser* browser = Browser::Create(dom_ui_->GetProfile());
  browser->OpenURL(GURL(chrome::kAboutTermsURL),
                   GURL(), NEW_FOREGROUND_TAB, PageTransition::LINK);
  browser->window()->Show();
}

void PluginsDOMHandler::Observe(NotificationType type,
                                const NotificationSource& source,
                                const NotificationDetails& details) {
  DCHECK_EQ(NotificationType::PLUGIN_ENABLE_STATUS_CHANGED, type.value);
  LoadPlugins();
}

void PluginsDOMHandler::LoadPluginsOnFileThread(ListWrapper* wrapper,
                                                Task* task) {
  wrapper->list = PluginUpdater::GetPluginUpdater()->GetPluginGroupsData();
  BrowserThread::PostTask(BrowserThread::UI, FROM_HERE, task);
  BrowserThread::PostTask(
      BrowserThread::UI,
      FROM_HERE,
      NewRunnableFunction(&PluginsDOMHandler::EnsureListDeleted, wrapper));
}

void PluginsDOMHandler::EnsureListDeleted(ListWrapper* wrapper) {
  delete wrapper->list;
  delete wrapper;
}

void PluginsDOMHandler::LoadPlugins() {
  if (!get_plugins_factory_.empty())
    return;

  ListWrapper* wrapper = new ListWrapper;
  wrapper->list = NULL;
  Task* task = get_plugins_factory_.NewRunnableMethod(
          &PluginsDOMHandler::PluginsLoaded, wrapper);

  BrowserThread::PostTask(
      BrowserThread::FILE,
      FROM_HERE,
      NewRunnableFunction(
          &PluginsDOMHandler::LoadPluginsOnFileThread, wrapper, task));
}

void PluginsDOMHandler::PluginsLoaded(ListWrapper* wrapper) {
  DictionaryValue results;
  results.Set("plugins", wrapper->list);
  wrapper->list = NULL;  // So it doesn't get deleted.
  dom_ui_->CallJavascriptFunction(L"returnPluginsData", results);
}

}  // namespace

///////////////////////////////////////////////////////////////////////////////
//
// PluginsUI
//
///////////////////////////////////////////////////////////////////////////////

PluginsUI::PluginsUI(TabContents* contents) : DOMUI(contents) {
  AddMessageHandler((new PluginsDOMHandler())->Attach(this));

  PluginsUIHTMLSource* html_source = new PluginsUIHTMLSource();

  // Set up the chrome://plugins/ source.
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      NewRunnableMethod(Singleton<ChromeURLDataManager>::get(),
          &ChromeURLDataManager::AddDataSource,
          make_scoped_refptr(html_source)));
}


// static
RefCountedMemory* PluginsUI::GetFaviconResourceBytes() {
  return ResourceBundle::GetSharedInstance().
      LoadDataResourceBytes(IDR_PLUGIN);
}

// static
void PluginsUI::RegisterUserPrefs(PrefService* prefs) {
  FilePath internal_dir;
  PathService::Get(chrome::DIR_INTERNAL_PLUGINS, &internal_dir);
  prefs->RegisterFilePathPref(prefs::kPluginsLastInternalDirectory,
                              internal_dir);

  prefs->RegisterListPref(prefs::kPluginsPluginsBlacklist);
  prefs->RegisterListPref(prefs::kPluginsPluginsList);
  prefs->RegisterBooleanPref(prefs::kPluginsEnabledInternalPDF, false);
}
