// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/dom_ui/conflicts_ui.h"

#if defined(OS_WIN)

#include <string>

#include "app/l10n_util.h"
#include "app/resource_bundle.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/dom_ui/chrome_url_data_manager.h"
#include "chrome/browser/enumerate_modules_model_win.h"
#include "chrome/common/jstemplate_builder.h"
#include "chrome/common/notification_observer.h"
#include "chrome/common/notification_registrar.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/url_constants.h"
#include "grit/browser_resources.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"

namespace {

////////////////////////////////////////////////////////////////////////////////
//
// ConflictsUIHTMLSource
//
////////////////////////////////////////////////////////////////////////////////

class ConflictsUIHTMLSource : public ChromeURLDataManager::DataSource {
 public:
  ConflictsUIHTMLSource()
      : DataSource(chrome::kChromeUIConflictsHost, MessageLoop::current()) {}

  // Called when the network layer has requested a resource underneath
  // the path we registered.
  virtual void StartDataRequest(const std::string& path,
                                bool is_off_the_record,
                                int request_id);

  virtual std::string GetMimeType(const std::string&) const {
    return "text/html";
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ConflictsUIHTMLSource);
};

void ConflictsUIHTMLSource::StartDataRequest(const std::string& path,
                                             bool is_off_the_record,
                                             int request_id) {
  // Strings used in the JsTemplate file.
  DictionaryValue localized_strings;
  localized_strings.SetString("modulesLongTitle",
      l10n_util::GetStringUTF16(IDS_CONFLICTS_CHECK_PAGE_TITLE_LONG));
  localized_strings.SetString("modulesBlurb",
      l10n_util::GetStringUTF16(IDS_CONFLICTS_EXPLANATION_TEXT));
  localized_strings.SetString("moduleSuspectedBad",
      l10n_util::GetStringUTF16(IDS_CONFLICTS_CHECK_WARNING_SUSPECTED));
  localized_strings.SetString("moduleConfirmedBad",
      l10n_util::GetStringUTF16(IDS_CONFLICTS_CHECK_WARNING_CONFIRMED));
  localized_strings.SetString("helpCenterLink",
      l10n_util::GetStringUTF16(IDS_CONFLICTS_HELP_CENTER_LINK));
  localized_strings.SetString("investigatingText",
      l10n_util::GetStringUTF16(IDS_CONFLICTS_CHECK_INVESTIGATING));
  localized_strings.SetString("modulesNoneLoaded",
      l10n_util::GetStringUTF16(IDS_CONFLICTS_NO_MODULES_LOADED));
  localized_strings.SetString("headerSoftware",
      l10n_util::GetStringUTF16(IDS_CONFLICTS_HEADER_SOFTWARE));
  localized_strings.SetString("headerSignedBy",
      l10n_util::GetStringUTF16(IDS_CONFLICTS_HEADER_SIGNED_BY));
  localized_strings.SetString("headerLocation",
      l10n_util::GetStringUTF16(IDS_CONFLICTS_HEADER_LOCATION));
  localized_strings.SetString("headerWarning",
      l10n_util::GetStringUTF16(IDS_CONFLICTS_HEADER_WARNING));
  localized_strings.SetString("headerHelpTip",
      l10n_util::GetStringUTF16(IDS_CONFLICTS_HEADER_HELP_TIP));

  ChromeURLDataManager::DataSource::SetFontAndTextDirection(&localized_strings);

  static const base::StringPiece flags_html(
      ResourceBundle::GetSharedInstance().GetRawDataResource(
          IDR_ABOUT_CONFLICTS_HTML));
  std::string full_html(flags_html.data(), flags_html.size());
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
// ConflictsDOMHandler
//
////////////////////////////////////////////////////////////////////////////////

// The handler for Javascript messages for the about:flags page.
class ConflictsDOMHandler : public DOMMessageHandler,
                            public NotificationObserver {
 public:
  ConflictsDOMHandler() {}
  virtual ~ConflictsDOMHandler() {}

  // DOMMessageHandler implementation.
  virtual void RegisterMessages();

  // Callback for the "requestModuleList" message.
  void HandleRequestModuleList(const ListValue* args);

 private:
  void SendModuleList();

  void Observe(NotificationType type,
               const NotificationSource& source,
               const NotificationDetails& details);

  NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(ConflictsDOMHandler);
};

void ConflictsDOMHandler::RegisterMessages() {
  dom_ui_->RegisterMessageCallback("requestModuleList",
      NewCallback(this, &ConflictsDOMHandler::HandleRequestModuleList));
}

void ConflictsDOMHandler::HandleRequestModuleList(const ListValue* args) {
  // This request is handled asynchronously. See Observe for when we reply back.
  registrar_.Add(this, NotificationType::MODULE_LIST_ENUMERATED,
                 NotificationService::AllSources());
  EnumerateModulesModel::GetSingleton()->ScanNow();
}

void ConflictsDOMHandler::SendModuleList() {
  EnumerateModulesModel* loaded_modules = EnumerateModulesModel::GetSingleton();
  ListValue* list = loaded_modules->GetModuleList();
  DictionaryValue results;
  results.Set("moduleList", list);

  // Add the section title and the total count for bad modules found.
  int confirmed_bad = loaded_modules->confirmed_bad_modules_detected();
  int suspected_bad = loaded_modules->suspected_bad_modules_detected();
  string16 table_title;
  if (!confirmed_bad && !suspected_bad) {
    table_title += l10n_util::GetStringFUTF16(
        IDS_CONFLICTS_CHECK_PAGE_TABLE_TITLE_SUFFIX_ONE,
            base::IntToString16(list->GetSize()));
  } else {
    table_title += l10n_util::GetStringFUTF16(
        IDS_CONFLICTS_CHECK_PAGE_TABLE_TITLE_SUFFIX_TWO,
            base::IntToString16(list->GetSize()),
            base::IntToString16(confirmed_bad),
            base::IntToString16(suspected_bad));
  }
  results.SetString("modulesTableTitle", table_title);

  dom_ui_->CallJavascriptFunction(L"returnModuleList", results);
}

void ConflictsDOMHandler::Observe(NotificationType type,
                                  const NotificationSource& source,
                                  const NotificationDetails& details) {
  switch (type.value) {
    case NotificationType::MODULE_LIST_ENUMERATED:
      SendModuleList();
      registrar_.RemoveAll();
      break;
    default:
      NOTREACHED();
      break;
  }
}

}  // namespace

///////////////////////////////////////////////////////////////////////////////
//
// ConflictsUI
//
///////////////////////////////////////////////////////////////////////////////

ConflictsUI::ConflictsUI(TabContents* contents) : DOMUI(contents) {
  AddMessageHandler((new ConflictsDOMHandler())->Attach(this));

  ConflictsUIHTMLSource* html_source = new ConflictsUIHTMLSource();

  // Set up the about:conflicts source.
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      NewRunnableMethod(Singleton<ChromeURLDataManager>::get(),
          &ChromeURLDataManager::AddDataSource,
          make_scoped_refptr(html_source)));
}

// static
RefCountedMemory* ConflictsUI::GetFaviconResourceBytes() {
  return ResourceBundle::GetSharedInstance().
      LoadDataResourceBytes(IDR_CONFLICT_MENU);
}

#endif
