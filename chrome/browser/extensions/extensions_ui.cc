// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extensions_ui.h"

#include "app/l10n_util.h"
#include "app/resource_bundle.h"
#include "base/base64.h"
#include "base/callback.h"
#include "base/file_util.h"
#include "base/string_util.h"
#include "base/thread.h"
#include "chrome/browser/browser.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/debugger/devtools_manager.h"
#include "chrome/browser/debugger/devtools_toggle_action.h"
#include "chrome/browser/extensions/crx_installer.h"
#include "chrome/browser/extensions/extension_disabled_infobar_delegate.h"
#include "chrome/browser/extensions/extension_error_reporter.h"
#include "chrome/browser/extensions/extension_function_dispatcher.h"
#include "chrome/browser/extensions/extension_host.h"
#include "chrome/browser/extensions/extension_message_service.h"
#include "chrome/browser/extensions/extensions_service.h"
#include "chrome/browser/extensions/extension_updater.h"
#include "chrome/browser/google_util.h"
#include "chrome/browser/pref_service.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/renderer_host/render_process_host.h"
#include "chrome/browser/renderer_host/render_widget_host.h"
#include "chrome/browser/renderer_host/render_view_host.h"
#include "chrome/browser/tab_contents/background_contents.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/browser/tab_contents/tab_contents_view.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/user_script.h"
#include "chrome/common/extensions/url_pattern.h"
#include "chrome/common/jstemplate_builder.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/notification_type.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "gfx/codec/png_codec.h"
#include "gfx/color_utils.h"
#include "gfx/skbitmap_operations.h"
#include "grit/browser_resources.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "net/base/net_util.h"
#include "webkit/glue/image_decoder.h"

namespace {

static bool ShouldShowExtension(Extension* extension) {
  // Don't show the themes since this page's UI isn't really useful for
  // themes.
  if (extension->is_theme())
    return false;

  // Don't show component extensions because they are only extensions as an
  // implementation detail of Chrome.
  if (extension->location() == Extension::COMPONENT)
    return false;

  return true;
}

}

////////////////////////////////////////////////////////////////////////////////
//
// ExtensionsHTMLSource
//
////////////////////////////////////////////////////////////////////////////////

ExtensionsUIHTMLSource::ExtensionsUIHTMLSource()
    : DataSource(chrome::kChromeUIExtensionsHost, MessageLoop::current()) {
}

void ExtensionsUIHTMLSource::StartDataRequest(const std::string& path,
    bool is_off_the_record, int request_id) {
  DictionaryValue localized_strings;
  localized_strings.SetString(L"title",
      l10n_util::GetString(IDS_EXTENSIONS_TITLE));
  localized_strings.SetString(L"devModeLink",
      l10n_util::GetString(IDS_EXTENSIONS_DEVELOPER_MODE_LINK));
  localized_strings.SetString(L"devModePrefix",
      l10n_util::GetString(IDS_EXTENSIONS_DEVELOPER_MODE_PREFIX));
  localized_strings.SetString(L"loadUnpackedButton",
      l10n_util::GetString(IDS_EXTENSIONS_LOAD_UNPACKED_BUTTON));
  localized_strings.SetString(L"packButton",
      l10n_util::GetString(IDS_EXTENSIONS_PACK_BUTTON));
  localized_strings.SetString(L"updateButton",
      l10n_util::GetString(IDS_EXTENSIONS_UPDATE_BUTTON));
  localized_strings.SetString(L"noExtensions",
      l10n_util::GetString(IDS_EXTENSIONS_NONE_INSTALLED));
  localized_strings.SetString(L"suggestGallery",
      l10n_util::GetStringF(IDS_EXTENSIONS_NONE_INSTALLED_SUGGEST_GALLERY,
          std::wstring(L"<a href='") +
          ASCIIToWide(google_util::AppendGoogleLocaleParam(
              GURL(Extension::ChromeStoreURL())).spec()) + L"'>",
          L"</a>"));
  localized_strings.SetString(L"getMoreExtensions",
      std::wstring(L"<a href='") +
          ASCIIToWide(google_util::AppendGoogleLocaleParam(
              GURL(Extension::ChromeStoreURL())).spec()) + L"'>" +
          l10n_util::GetString(IDS_GET_MORE_EXTENSIONS) +
          L"</a>");
  localized_strings.SetString(L"extensionDisabled",
      l10n_util::GetString(IDS_EXTENSIONS_DISABLED_EXTENSION));
  localized_strings.SetString(L"inDevelopment",
      l10n_util::GetString(IDS_EXTENSIONS_IN_DEVELOPMENT));
  localized_strings.SetString(L"extensionId",
      l10n_util::GetString(IDS_EXTENSIONS_ID));
  localized_strings.SetString(L"extensionVersion",
      l10n_util::GetString(IDS_EXTENSIONS_VERSION));
  localized_strings.SetString(L"inspectViews",
      l10n_util::GetString(IDS_EXTENSIONS_INSPECT_VIEWS));
  localized_strings.SetString(L"inspectPopupsInstructions",
      l10n_util::GetString(IDS_EXTENSIONS_INSPECT_POPUPS_INSTRUCTIONS));
  localized_strings.SetString(L"disable",
      l10n_util::GetString(IDS_EXTENSIONS_DISABLE));
  localized_strings.SetString(L"enable",
      l10n_util::GetString(IDS_EXTENSIONS_ENABLE));
  localized_strings.SetString(L"enableIncognito",
      l10n_util::GetString(IDS_EXTENSIONS_ENABLE_INCOGNITO));
  localized_strings.SetString(L"allowFileAccess",
      l10n_util::GetString(IDS_EXTENSIONS_ALLOW_FILE_ACCESS));
  localized_strings.SetString(L"incognitoWarning",
      l10n_util::GetString(IDS_EXTENSIONS_INCOGNITO_WARNING));
  localized_strings.SetString(L"reload",
      l10n_util::GetString(IDS_EXTENSIONS_RELOAD));
  localized_strings.SetString(L"uninstall",
      l10n_util::GetString(IDS_EXTENSIONS_UNINSTALL));
  localized_strings.SetString(L"options",
      l10n_util::GetString(IDS_EXTENSIONS_OPTIONS));
  localized_strings.SetString(L"packDialogTitle",
      l10n_util::GetString(IDS_EXTENSION_PACK_DIALOG_TITLE));
  localized_strings.SetString(L"packDialogHeading",
      l10n_util::GetString(IDS_EXTENSION_PACK_DIALOG_HEADING));
  localized_strings.SetString(L"rootDirectoryLabel",
      l10n_util::GetString(IDS_EXTENSION_PACK_DIALOG_ROOT_DIRECTORY_LABEL));
  localized_strings.SetString(L"packDialogBrowse",
      l10n_util::GetString(IDS_EXTENSION_PACK_DIALOG_BROWSE));
  localized_strings.SetString(L"privateKeyLabel",
      l10n_util::GetString(IDS_EXTENSION_PACK_DIALOG_PRIVATE_KEY_LABEL));
  localized_strings.SetString(L"okButton",
      l10n_util::GetString(IDS_OK));
  localized_strings.SetString(L"cancelButton",
      l10n_util::GetString(IDS_CANCEL));

  SetFontAndTextDirection(&localized_strings);

  static const base::StringPiece extensions_html(
      ResourceBundle::GetSharedInstance().GetRawDataResource(
          IDR_EXTENSIONS_UI_HTML));
  std::string full_html(extensions_html.data(), extensions_html.size());
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
// ExtensionsDOMHandler::IconLoader
//
////////////////////////////////////////////////////////////////////////////////

ExtensionsDOMHandler::IconLoader::IconLoader(ExtensionsDOMHandler* handler)
    : handler_(handler) {
}

void ExtensionsDOMHandler::IconLoader::LoadIcons(
    std::vector<ExtensionResource>* icons, DictionaryValue* json) {
  ChromeThread::PostTask(
      ChromeThread::FILE, FROM_HERE,
      NewRunnableMethod(this,
          &IconLoader::LoadIconsOnFileThread, icons, json));
}

void ExtensionsDOMHandler::IconLoader::Cancel() {
  handler_ = NULL;
}

void ExtensionsDOMHandler::IconLoader::LoadIconsOnFileThread(
    std::vector<ExtensionResource>* icons, DictionaryValue* json) {
  scoped_ptr<std::vector<ExtensionResource> > icons_deleter(icons);
  scoped_ptr<DictionaryValue> json_deleter(json);

  ListValue* extensions = NULL;
  CHECK(json->GetList(L"extensions", &extensions));

  for (size_t i = 0; i < icons->size(); ++i) {
    DictionaryValue* extension = NULL;
    CHECK(extensions->GetDictionary(static_cast<int>(i), &extension));

    // Read the file.
    std::string file_contents;
    if (icons->at(i).relative_path().empty() ||
        !file_util::ReadFileToString(icons->at(i).GetFilePath(),
                                     &file_contents)) {
      // If there's no icon, default to the puzzle icon. This is safe to do from
      // the file thread.
      file_contents = ResourceBundle::GetSharedInstance().GetRawDataResource(
          IDR_EXTENSION_DEFAULT_ICON).as_string();
    }

    // If the extension is disabled, we desaturate the icon to add to the
    // disabledness effect.
    bool enabled = false;
    CHECK(extension->GetBoolean(L"enabled", &enabled));
    if (!enabled) {
      const unsigned char* data =
          reinterpret_cast<const unsigned char*>(file_contents.data());
      webkit_glue::ImageDecoder decoder;
      scoped_ptr<SkBitmap> decoded(new SkBitmap());
      *decoded = decoder.Decode(data, file_contents.length());

      // Desaturate the icon and lighten it a bit.
      color_utils::HSL shift = {-1, 0, 0.6};
      *decoded = SkBitmapOperations::CreateHSLShiftedBitmap(*decoded, shift);

      std::vector<unsigned char> output;
      gfx::PNGCodec::EncodeBGRASkBitmap(*decoded, false, &output);

      // Lame, but we must make a copy of this now, because base64 doesn't take
      // the same input type.
      file_contents.assign(reinterpret_cast<char*>(&output.front()),
                           output.size());
    }

    // Create a data URL (all icons are converted to PNGs during unpacking).
    std::string base64_encoded;
    base::Base64Encode(file_contents, &base64_encoded);
    GURL icon_url("data:image/png;base64," + base64_encoded);

    extension->SetString(L"icon", icon_url.spec());
  }

  ChromeThread::PostTask(
      ChromeThread::UI, FROM_HERE,
      NewRunnableMethod(this, &IconLoader::ReportResultOnUIThread,
                        json_deleter.release()));
}

void ExtensionsDOMHandler::IconLoader::ReportResultOnUIThread(
    DictionaryValue* json) {
  if (handler_)
    handler_->OnIconsLoaded(json);
}


///////////////////////////////////////////////////////////////////////////////
//
// ExtensionsDOMHandler
//
///////////////////////////////////////////////////////////////////////////////

ExtensionsDOMHandler::ExtensionsDOMHandler(ExtensionsService* extension_service)
    : extensions_service_(extension_service),
      ignore_notifications_(false),
      deleting_rvh_(NULL) {
}

void ExtensionsDOMHandler::RegisterMessages() {
  dom_ui_->RegisterMessageCallback("requestExtensionsData",
      NewCallback(this, &ExtensionsDOMHandler::HandleRequestExtensionsData));
  dom_ui_->RegisterMessageCallback("toggleDeveloperMode",
      NewCallback(this, &ExtensionsDOMHandler::HandleToggleDeveloperMode));
  dom_ui_->RegisterMessageCallback("inspect",
      NewCallback(this, &ExtensionsDOMHandler::HandleInspectMessage));
  dom_ui_->RegisterMessageCallback("reload",
      NewCallback(this, &ExtensionsDOMHandler::HandleReloadMessage));
  dom_ui_->RegisterMessageCallback("enable",
      NewCallback(this, &ExtensionsDOMHandler::HandleEnableMessage));
  dom_ui_->RegisterMessageCallback("enableIncognito",
      NewCallback(this, &ExtensionsDOMHandler::HandleEnableIncognitoMessage));
  dom_ui_->RegisterMessageCallback("allowFileAccess",
      NewCallback(this, &ExtensionsDOMHandler::HandleAllowFileAccessMessage));
  dom_ui_->RegisterMessageCallback("uninstall",
      NewCallback(this, &ExtensionsDOMHandler::HandleUninstallMessage));
  dom_ui_->RegisterMessageCallback("options",
    NewCallback(this, &ExtensionsDOMHandler::HandleOptionsMessage));
  dom_ui_->RegisterMessageCallback("load",
      NewCallback(this, &ExtensionsDOMHandler::HandleLoadMessage));
  dom_ui_->RegisterMessageCallback("pack",
      NewCallback(this, &ExtensionsDOMHandler::HandlePackMessage));
  dom_ui_->RegisterMessageCallback("autoupdate",
      NewCallback(this, &ExtensionsDOMHandler::HandleAutoUpdateMessage));
  dom_ui_->RegisterMessageCallback("selectFilePath",
      NewCallback(this, &ExtensionsDOMHandler::HandleSelectFilePathMessage));
}

void ExtensionsDOMHandler::HandleRequestExtensionsData(const Value* value) {
  DictionaryValue* results = new DictionaryValue();

  // Add the extensions to the results structure.
  ListValue *extensions_list = new ListValue();

  // Stores the icon resource for each of the extensions in extensions_list. We
  // build up a list of them here, then load them on the file thread in
  // ::LoadIcons().
  std::vector<ExtensionResource>* extension_icons =
      new std::vector<ExtensionResource>();

  ExtensionProcessManager* process_manager =
      extensions_service_->profile()->GetExtensionProcessManager();
  const ExtensionList* extensions = extensions_service_->extensions();
  for (ExtensionList::const_iterator extension = extensions->begin();
       extension != extensions->end(); ++extension) {
    if (ShouldShowExtension(*extension)) {
      RenderProcessHost* process =
          process_manager->GetExtensionProcess((*extension)->url());
      extensions_list->Append(CreateExtensionDetailValue(
          extensions_service_.get(),
          *extension,
          GetActivePagesForExtension(process, *extension),
          true));  // enabled
      extension_icons->push_back(PickExtensionIcon(*extension));
    }
  }
  extensions = extensions_service_->disabled_extensions();
  for (ExtensionList::const_iterator extension = extensions->begin();
       extension != extensions->end(); ++extension) {
    if (ShouldShowExtension(*extension)) {
      RenderProcessHost* process =
          process_manager->GetExtensionProcess((*extension)->url());
      extensions_list->Append(CreateExtensionDetailValue(
          extensions_service_.get(),
          *extension,
          GetActivePagesForExtension(process, *extension),
          false));  // enabled
      extension_icons->push_back(PickExtensionIcon(*extension));
    }
  }
  results->Set(L"extensions", extensions_list);

  bool developer_mode = dom_ui_->GetProfile()->GetPrefs()
      ->GetBoolean(prefs::kExtensionsUIDeveloperMode);
  results->SetBoolean(L"developerMode", developer_mode);

  if (icon_loader_.get())
    icon_loader_->Cancel();

  icon_loader_ = new IconLoader(this);
  icon_loader_->LoadIcons(extension_icons, results);
}

void ExtensionsDOMHandler::OnIconsLoaded(DictionaryValue* json) {
  dom_ui_->CallJavascriptFunction(L"returnExtensionsData", *json);
  delete json;

  // Register for notifications that we need to reload the page.
  registrar_.RemoveAll();
  registrar_.Add(this, NotificationType::EXTENSION_LOADED,
      NotificationService::AllSources());
  registrar_.Add(this, NotificationType::EXTENSION_PROCESS_CREATED,
      NotificationService::AllSources());
  registrar_.Add(this, NotificationType::EXTENSION_UNLOADED,
      NotificationService::AllSources());
  registrar_.Add(this, NotificationType::EXTENSION_UNLOADED_DISABLED,
      NotificationService::AllSources());
  registrar_.Add(this, NotificationType::EXTENSION_UPDATE_DISABLED,
      NotificationService::AllSources());
  registrar_.Add(this, NotificationType::EXTENSION_FUNCTION_DISPATCHER_CREATED,
      NotificationService::AllSources());
  registrar_.Add(this,
      NotificationType::EXTENSION_FUNCTION_DISPATCHER_DESTROYED,
      NotificationService::AllSources());
  registrar_.Add(this,
      NotificationType::NAV_ENTRY_COMMITTED,
      NotificationService::AllSources());
  registrar_.Add(this,
      NotificationType::RENDER_VIEW_HOST_DELETED,
      NotificationService::AllSources());
  registrar_.Add(this,
      NotificationType::BACKGROUND_CONTENTS_NAVIGATED,
      NotificationService::AllSources());
  registrar_.Add(this,
      NotificationType::BACKGROUND_CONTENTS_DELETED,
      NotificationService::AllSources());
}

ExtensionResource ExtensionsDOMHandler::PickExtensionIcon(
    Extension* extension) {
  // Try to fetch the medium sized icon, then (if missing) go for the large one.
  const std::map<int, std::string>& icons = extension->icons();
  std::map<int, std::string>::const_iterator iter =
      icons.find(Extension::EXTENSION_ICON_MEDIUM);
  if (iter == icons.end())
    iter = icons.find(Extension::EXTENSION_ICON_LARGE);
  if (iter != icons.end())
    return extension->GetResource(iter->second);
  else
    return ExtensionResource();
}

ExtensionInstallUI* ExtensionsDOMHandler::GetExtensionInstallUI() {
  if (!install_ui_.get())
    install_ui_.reset(new ExtensionInstallUI(dom_ui_->GetProfile()));
  return install_ui_.get();
}

void ExtensionsDOMHandler::HandleToggleDeveloperMode(const Value* value) {
  bool developer_mode = dom_ui_->GetProfile()->GetPrefs()
      ->GetBoolean(prefs::kExtensionsUIDeveloperMode);
  dom_ui_->GetProfile()->GetPrefs()->SetBoolean(
      prefs::kExtensionsUIDeveloperMode, !developer_mode);
}

void ExtensionsDOMHandler::HandleInspectMessage(const Value* value) {
  std::string render_process_id_str;
  std::string render_view_id_str;
  int render_process_id;
  int render_view_id;
  CHECK(value->IsType(Value::TYPE_LIST));
  const ListValue* list = static_cast<const ListValue*>(value);
  CHECK(list->GetSize() == 2);
  CHECK(list->GetString(0, &render_process_id_str));
  CHECK(list->GetString(1, &render_view_id_str));
  CHECK(StringToInt(render_process_id_str, &render_process_id));
  CHECK(StringToInt(render_view_id_str, &render_view_id));
  RenderViewHost* host = RenderViewHost::FromID(render_process_id,
                                                render_view_id);
  if (!host) {
    // This can happen if the host has gone away since the page was displayed.
    return;
  }

  DevToolsManager::GetInstance()->ToggleDevToolsWindow(
      host,
      DEVTOOLS_TOGGLE_ACTION_SHOW_CONSOLE);
}

void ExtensionsDOMHandler::HandleReloadMessage(const Value* value) {
  CHECK(value->IsType(Value::TYPE_LIST));
  const ListValue* list = static_cast<const ListValue*>(value);
  CHECK(list->GetSize() == 1);
  std::string extension_id;
  CHECK(list->GetString(0, &extension_id));
  extensions_service_->ReloadExtension(extension_id);
}

void ExtensionsDOMHandler::HandleEnableMessage(const Value* value) {
  CHECK(value->IsType(Value::TYPE_LIST));
  const ListValue* list = static_cast<const ListValue*>(value);
  CHECK(list->GetSize() == 2);
  std::string extension_id, enable_str;
  CHECK(list->GetString(0, &extension_id));
  CHECK(list->GetString(1, &enable_str));
  if (enable_str == "true") {
    ExtensionPrefs* prefs = extensions_service_->extension_prefs();
    if (prefs->DidExtensionEscalatePermissions(extension_id)) {
      Extension* extension =
          extensions_service_->GetExtensionById(extension_id, true);
      ShowExtensionDisabledDialog(extensions_service_,
                                  dom_ui_->GetProfile(), extension);
    } else {
      extensions_service_->EnableExtension(extension_id);
    }
  } else {
    extensions_service_->DisableExtension(extension_id);
  }
}

void ExtensionsDOMHandler::HandleEnableIncognitoMessage(const Value* value) {
  CHECK(value->IsType(Value::TYPE_LIST));
  const ListValue* list = static_cast<const ListValue*>(value);
  CHECK(list->GetSize() == 2);
  std::string extension_id, enable_str;
  CHECK(list->GetString(0, &extension_id));
  CHECK(list->GetString(1, &enable_str));
  Extension* extension = extensions_service_->GetExtensionById(extension_id,
                                                               true);
  DCHECK(extension);

  // Flipping the incognito bit will generate unload/load notifications for the
  // extension, but we don't want to reload the page, because a) we've already
  // updated the UI to reflect the change, and b) we want the yellow warning
  // text to stay until the user has left the page.
  //
  // TODO(aa): This creates crapiness in some cases. For example, in a main
  // window, when toggling this, the browser action will flicker because it gets
  // unloaded, then reloaded. It would be better to have a dedicated
  // notification for this case.
  //
  // Bug: http://crbug.com/41384
  ignore_notifications_ = true;
  extensions_service_->SetIsIncognitoEnabled(extension, enable_str == "true");
  ignore_notifications_ = false;
}

void ExtensionsDOMHandler::HandleAllowFileAccessMessage(const Value* value) {
  CHECK(value->IsType(Value::TYPE_LIST));
  const ListValue* list = static_cast<const ListValue*>(value);
  CHECK(list->GetSize() == 2);
  std::string extension_id, allow_str;
  CHECK(list->GetString(0, &extension_id));
  CHECK(list->GetString(1, &allow_str));
  Extension* extension = extensions_service_->GetExtensionById(extension_id,
                                                               true);
  DCHECK(extension);

  extensions_service_->SetAllowFileAccess(extension, allow_str == "true");
}

void ExtensionsDOMHandler::HandleUninstallMessage(const Value* value) {
  CHECK(value->IsType(Value::TYPE_LIST));
  const ListValue* list = static_cast<const ListValue*>(value);
  CHECK(list->GetSize() == 1);
  std::string extension_id;
  CHECK(list->GetString(0, &extension_id));

  Extension *extension =
      extensions_service_->GetExtensionById(extension_id, true);
  if (!extension)
    return;

  if (!extension_id_prompting_.empty())
    return;  // only one prompt at a time

  extension_id_prompting_ = extension_id;

  GetExtensionInstallUI()->ConfirmUninstall(this, extension);
}

void ExtensionsDOMHandler::InstallUIProceed(bool create_app_shortcut) {
  // We only ever use ExtensionInstallUI for uninstalling, which should never
  // result in it telling us to create a shortcut.
  DCHECK(!create_app_shortcut);

  DCHECK(!extension_id_prompting_.empty());

  // The extension can be uninstalled in another window while the UI was
  // showing. Do nothing in that case.
  Extension* extension =
      extensions_service_->GetExtensionById(extension_id_prompting_, true);
  if (!extension)
    return;

  extensions_service_->UninstallExtension(extension_id_prompting_,
                                          false /* external_uninstall */);
  extension_id_prompting_ = "";
}

void ExtensionsDOMHandler::InstallUIAbort() {
  extension_id_prompting_ = "";
}

void ExtensionsDOMHandler::HandleOptionsMessage(const Value* value) {
  CHECK(value->IsType(Value::TYPE_LIST));
  const ListValue* list = static_cast<const ListValue*>(value);
  CHECK(list->GetSize() == 1);
  std::string extension_id;
  CHECK(list->GetString(0, &extension_id));
  Extension *extension =
      extensions_service_->GetExtensionById(extension_id, false);
  if (!extension || extension->options_url().is_empty()) {
    return;
  }
  dom_ui_->GetProfile()->GetExtensionProcessManager()->OpenOptionsPage(
      extension, NULL);
}

void ExtensionsDOMHandler::HandleLoadMessage(const Value* value) {
  FilePath::StringType string_path;
  CHECK(value->IsType(Value::TYPE_LIST));
  const ListValue* list = static_cast<const ListValue*>(value);
  CHECK(list->GetSize() == 1) << list->GetSize();
  CHECK(list->GetString(0, &string_path));
  extensions_service_->LoadExtension(FilePath(string_path));
}

void ExtensionsDOMHandler::ShowAlert(const std::string& message) {
  ListValue arguments;
  arguments.Append(Value::CreateStringValue(message));
  dom_ui_->CallJavascriptFunction(L"alert", arguments);
}

void ExtensionsDOMHandler::HandlePackMessage(const Value* value) {
  std::wstring extension_path;
  std::wstring private_key_path;
  CHECK(value->IsType(Value::TYPE_LIST));
  const ListValue* list = static_cast<const ListValue*>(value);
  CHECK(list->GetSize() == 2);
  CHECK(list->GetString(0, &extension_path));
  CHECK(list->GetString(1, &private_key_path));

  FilePath root_directory = FilePath::FromWStringHack(extension_path);
  FilePath key_file = FilePath::FromWStringHack(private_key_path);

  if (root_directory.empty()) {
    if (extension_path.empty()) {
      ShowAlert(l10n_util::GetStringUTF8(
          IDS_EXTENSION_PACK_DIALOG_ERROR_ROOT_REQUIRED));
    } else {
      ShowAlert(l10n_util::GetStringUTF8(
          IDS_EXTENSION_PACK_DIALOG_ERROR_ROOT_INVALID));
    }

    return;
  }

  if (!private_key_path.empty() && key_file.empty()) {
    ShowAlert(l10n_util::GetStringUTF8(
        IDS_EXTENSION_PACK_DIALOG_ERROR_KEY_INVALID));
    return;
  }

  pack_job_ = new PackExtensionJob(this, root_directory, key_file);
  pack_job_->Start();
}

void ExtensionsDOMHandler::OnPackSuccess(const FilePath& crx_file,
                                         const FilePath& pem_file) {
  std::string message;
  if (!pem_file.empty()) {
    message = WideToUTF8(l10n_util::GetStringF(
        IDS_EXTENSION_PACK_DIALOG_SUCCESS_BODY_NEW,
        crx_file.ToWStringHack(),
        pem_file.ToWStringHack()));
  } else {
    message = WideToUTF8(l10n_util::GetStringF(
        IDS_EXTENSION_PACK_DIALOG_SUCCESS_BODY_UPDATE,
        crx_file.ToWStringHack()));
  }
  ShowAlert(message);

  ListValue results;
  dom_ui_->CallJavascriptFunction(L"hidePackDialog", results);
}

void ExtensionsDOMHandler::OnPackFailure(const std::wstring& error) {
  ShowAlert(WideToUTF8(error));
}

void ExtensionsDOMHandler::HandleAutoUpdateMessage(const Value* value) {
  ExtensionUpdater* updater = extensions_service_->updater();
  if (updater) {
    updater->CheckNow();
  }
}

void ExtensionsDOMHandler::HandleSelectFilePathMessage(const Value* value) {
  std::string select_type;
  std::string operation;
  CHECK(value->IsType(Value::TYPE_LIST));
  const ListValue* list = static_cast<const ListValue*>(value);
  CHECK(list->GetSize() == 2);
  CHECK(list->GetString(0, &select_type));
  CHECK(list->GetString(1, &operation));

  SelectFileDialog::Type type = SelectFileDialog::SELECT_FOLDER;
  static SelectFileDialog::FileTypeInfo info;
  int file_type_index = 0;
  if (select_type == "file")
    type = SelectFileDialog::SELECT_OPEN_FILE;

  string16 select_title;
  if (operation == "load")
    select_title = l10n_util::GetStringUTF16(IDS_EXTENSION_LOAD_FROM_DIRECTORY);
  else if (operation == "packRoot")
    select_title = l10n_util::GetStringUTF16(
        IDS_EXTENSION_PACK_DIALOG_SELECT_ROOT);
  else if (operation == "pem") {
    select_title = l10n_util::GetStringUTF16(
        IDS_EXTENSION_PACK_DIALOG_SELECT_KEY);
    info.extensions.push_back(std::vector<FilePath::StringType>());
        info.extensions.front().push_back(FILE_PATH_LITERAL("pem"));
        info.extension_description_overrides.push_back(WideToUTF16(
            l10n_util::GetString(
                IDS_EXTENSION_PACK_DIALOG_KEY_FILE_TYPE_DESCRIPTION)));
        info.include_all_files = true;
    file_type_index = 1;
  } else {
    NOTREACHED();
    return;
  }

  load_extension_dialog_ = SelectFileDialog::Create(this);
  load_extension_dialog_->SelectFile(type, select_title, FilePath(), &info,
      file_type_index, FILE_PATH_LITERAL(""),
      dom_ui_->tab_contents()->view()->GetTopLevelNativeWindow(), NULL);
}


void ExtensionsDOMHandler::FileSelected(const FilePath& path, int index,
                                        void* params) {
  // Add the extensions to the results structure.
  ListValue results;
  results.Append(Value::CreateStringValue(path.value()));
  dom_ui_->CallJavascriptFunction(L"window.handleFilePathSelected", results);
}

void ExtensionsDOMHandler::Observe(NotificationType type,
                                   const NotificationSource& source,
                                   const NotificationDetails& details) {
  switch (type.value) {
    // We listen for notifications that will result in the page being
    // repopulated with data twice for the same event in certain cases.
    // For instance, EXTENSION_LOADED & EXTENSION_PROCESS_CREATED because
    // we don't know about the views for an extension at EXTENSION_LOADED, but
    // if we only listen to EXTENSION_PROCESS_CREATED, we'll miss extensions
    // that don't have a process at startup. Similarly, NAV_ENTRY_COMMITTED &
    // EXTENSION_FUNCTION_DISPATCHER_CREATED because we want to handle both
    // the case of live app pages (which don't have an EFD) and
    // chrome-extension:// urls which are served in a TabContents.
    //
    // Doing it this way gets everything but causes the page to be rendered
    // more than we need. It doesn't seem to result in any noticeable flicker.
    case NotificationType::RENDER_VIEW_HOST_DELETED:
      deleting_rvh_ = Details<RenderViewHost>(details).ptr();
      MaybeUpdateAfterNotification();
      break;
    case NotificationType::BACKGROUND_CONTENTS_DELETED:
      deleting_rvh_ = Details<BackgroundContents>(details)->render_view_host();
      MaybeUpdateAfterNotification();
      break;
    case NotificationType::EXTENSION_LOADED:
    case NotificationType::EXTENSION_PROCESS_CREATED:
    case NotificationType::EXTENSION_UNLOADED:
    case NotificationType::EXTENSION_UNLOADED_DISABLED:
    case NotificationType::EXTENSION_UPDATE_DISABLED:
    case NotificationType::EXTENSION_FUNCTION_DISPATCHER_CREATED:
    case NotificationType::EXTENSION_FUNCTION_DISPATCHER_DESTROYED:
    case NotificationType::NAV_ENTRY_COMMITTED:
    case NotificationType::BACKGROUND_CONTENTS_NAVIGATED:
      MaybeUpdateAfterNotification();
      break;
    default:
      NOTREACHED();
  }
}

void ExtensionsDOMHandler::MaybeUpdateAfterNotification() {
  if (!ignore_notifications_ && dom_ui_->tab_contents())
    HandleRequestExtensionsData(NULL);
  deleting_rvh_ = NULL;
}

static void CreateScriptFileDetailValue(
    const FilePath& extension_path, const UserScript::FileList& scripts,
    const wchar_t* key, DictionaryValue* script_data) {
  if (scripts.empty())
    return;

  ListValue *list = new ListValue();
  for (size_t i = 0; i < scripts.size(); ++i) {
    const UserScript::File& file = scripts[i];
    // TODO(cira): this information is not used on extension page yet. We
    // may want to display actual resource that got loaded, not default.
    list->Append(
        new StringValue(file.relative_path().value()));
  }
  script_data->Set(key, list);
}

// Static
DictionaryValue* ExtensionsDOMHandler::CreateContentScriptDetailValue(
  const UserScript& script, const FilePath& extension_path) {
  DictionaryValue* script_data = new DictionaryValue();
  CreateScriptFileDetailValue(extension_path, script.js_scripts(), L"js",
    script_data);
  CreateScriptFileDetailValue(extension_path, script.css_scripts(), L"css",
    script_data);

  // Get list of glob "matches" strings
  ListValue *url_pattern_list = new ListValue();
  const std::vector<URLPattern>& url_patterns = script.url_patterns();
  for (std::vector<URLPattern>::const_iterator url_pattern =
      url_patterns.begin();
    url_pattern != url_patterns.end(); ++url_pattern) {
    url_pattern_list->Append(new StringValue(url_pattern->GetAsString()));
  }

  script_data->Set(L"matches", url_pattern_list);

  return script_data;
}

static bool ExtensionWantsFileAccess(const Extension* extension) {
  for (UserScriptList::const_iterator it = extension->content_scripts().begin();
       it != extension->content_scripts().end(); ++it) {
    for (UserScript::PatternList::const_iterator pattern =
             it->url_patterns().begin();
         pattern != it->url_patterns().end(); ++pattern) {
      if (pattern->MatchesScheme(chrome::kFileScheme))
        return true;
    }
  }

  return false;
}

// Static
DictionaryValue* ExtensionsDOMHandler::CreateExtensionDetailValue(
    ExtensionsService* service, const Extension* extension,
    const std::vector<ExtensionPage>& pages, bool enabled) {
  DictionaryValue* extension_data = new DictionaryValue();

  extension_data->SetString(L"id", extension->id());
  extension_data->SetString(L"name", extension->name());
  extension_data->SetString(L"description", extension->description());
  extension_data->SetString(L"version", extension->version()->GetString());
  extension_data->SetBoolean(L"enabled", enabled);
  extension_data->SetBoolean(L"enabledIncognito",
      service ? service->IsIncognitoEnabled(extension) : false);
  extension_data->SetBoolean(L"wantsFileAccess",
      ExtensionWantsFileAccess(extension));
  extension_data->SetBoolean(L"allowFileAccess",
      service ? service->AllowFileAccess(extension) : false);
  extension_data->SetBoolean(L"allow_reload",
                             extension->location() == Extension::LOAD);

  // Determine the sort order: Extensions loaded through --load-extensions show
  // up at the top. Disabled extensions show up at the bottom.
  if (extension->location() == Extension::LOAD)
    extension_data->SetInteger(L"order", 1);
  else
    extension_data->SetInteger(L"order", 2);

  if (!extension->options_url().is_empty())
    extension_data->SetString(L"options_url", extension->options_url().spec());

  // Add list of content_script detail DictionaryValues.
  ListValue *content_script_list = new ListValue();
  UserScriptList content_scripts = extension->content_scripts();
  for (UserScriptList::const_iterator script = content_scripts.begin();
    script != content_scripts.end(); ++script) {
      content_script_list->Append(
          CreateContentScriptDetailValue(*script, extension->path()));
  }
  extension_data->Set(L"content_scripts", content_script_list);

  // Add permissions.
  ListValue *permission_list = new ListValue;
  std::vector<URLPattern> permissions = extension->host_permissions();
  for (std::vector<URLPattern>::iterator permission = permissions.begin();
       permission != permissions.end(); ++permission) {
    permission_list->Append(Value::CreateStringValue(
        permission->GetAsString()));
  }
  extension_data->Set(L"permissions", permission_list);

  // Add views
  ListValue* views = new ListValue;
  for (std::vector<ExtensionPage>::const_iterator iter = pages.begin();
       iter != pages.end(); ++iter) {
    DictionaryValue* view_value = new DictionaryValue;
    if (iter->url.scheme() == chrome::kExtensionScheme) {
      // No leading slash.
      view_value->SetString(L"path", iter->url.path().substr(1));
    } else {
      // For live pages, use the full URL.
      view_value->SetString(L"path", iter->url.spec());
    }
    view_value->SetInteger(L"renderViewId", iter->render_view_id);
    view_value->SetInteger(L"renderProcessId", iter->render_process_id);
    views->Append(view_value);
  }
  extension_data->Set(L"views", views);
  extension_data->SetBoolean(L"hasPopupAction",
      extension->browser_action() || extension->page_action());
  extension_data->SetString(L"galleryUrl", extension->GalleryUrl().spec());

  return extension_data;
}

std::vector<ExtensionPage> ExtensionsDOMHandler::GetActivePagesForExtension(
    RenderProcessHost* process,
    Extension* extension) {
  std::vector<ExtensionPage> result;
  if (!process)
    return result;

  RenderProcessHost::listeners_iterator iter = process->ListenersIterator();
  for (; !iter.IsAtEnd(); iter.Advance()) {
    const RenderWidgetHost* widget =
        static_cast<const RenderWidgetHost*>(iter.GetCurrentValue());
    DCHECK(widget);
    if (!widget || !widget->IsRenderView())
      continue;
    const RenderViewHost* host = static_cast<const RenderViewHost*>(widget);
    if (host == deleting_rvh_ ||
        ViewType::EXTENSION_POPUP == host->delegate()->GetRenderViewType())
      continue;

    GURL url = host->delegate()->GetURL();
    if (url.SchemeIs(chrome::kExtensionScheme)) {
      if (url.host() != extension->id())
        continue;
    } else if (!extension->web_extent().ContainsURL(url)) {
      continue;
    }

    result.push_back(ExtensionPage(url, process->id(), host->routing_id()));
  }

  return result;
}

ExtensionsDOMHandler::~ExtensionsDOMHandler() {
  if (pack_job_.get())
    pack_job_->ClearClient();

  if (icon_loader_.get())
    icon_loader_->Cancel();
}

// ExtensionsDOMHandler, public: -----------------------------------------------

ExtensionsUI::ExtensionsUI(TabContents* contents) : DOMUI(contents) {
  ExtensionsService *exstension_service =
      GetProfile()->GetOriginalProfile()->GetExtensionsService();

  ExtensionsDOMHandler* handler = new ExtensionsDOMHandler(exstension_service);
  AddMessageHandler(handler);
  handler->Attach(this);

  ExtensionsUIHTMLSource* html_source = new ExtensionsUIHTMLSource();

  // Set up the chrome://extensions/ source.
  ChromeThread::PostTask(
      ChromeThread::IO, FROM_HERE,
      NewRunnableMethod(
          Singleton<ChromeURLDataManager>::get(),
          &ChromeURLDataManager::AddDataSource,
          make_scoped_refptr(html_source)));
}

// static
RefCountedMemory* ExtensionsUI::GetFaviconResourceBytes() {
  return ResourceBundle::GetSharedInstance().
      LoadDataResourceBytes(IDR_PLUGIN);
}

// static
void ExtensionsUI::RegisterUserPrefs(PrefService* prefs) {
  prefs->RegisterBooleanPref(prefs::kExtensionsUIDeveloperMode, false);
}
