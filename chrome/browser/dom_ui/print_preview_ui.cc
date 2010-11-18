// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/dom_ui/print_preview_ui.h"

#include <algorithm>

#include "app/l10n_util.h"
#include "app/resource_bundle.h"
#include "base/message_loop.h"
#include "base/singleton.h"
#include "base/string_piece.h"
#include "base/values.h"
#include "chrome/browser/browser_thread.h"
#include "chrome/browser/dom_ui/chrome_url_data_manager.h"
#include "chrome/browser/dom_ui/dom_ui_theme_source.h"
#include "chrome/browser/dom_ui/print_preview_handler.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/common/jstemplate_builder.h"
#include "chrome/common/url_constants.h"

#include "grit/browser_resources.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"

namespace {

void SetLocalizedStrings(DictionaryValue* localized_strings) {
  localized_strings->SetString(std::string("title"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_TITLE));
  localized_strings->SetString(std::string("no-printer"),
      l10n_util::GetStringUTF8(IDS_PRINT_PREVIEW_NO_PRINTER));
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////
//
// PrintPreviewUIHTMLSource
//
////////////////////////////////////////////////////////////////////////////////

class PrintPreviewUIHTMLSource : public ChromeURLDataManager::DataSource {
 public:
  PrintPreviewUIHTMLSource();
  virtual ~PrintPreviewUIHTMLSource();

  // Called when the network layer has requested a resource underneath
  // the path we registered.
  virtual void StartDataRequest(const std::string& path,
                                bool is_off_the_record,
                                int request_id);
  virtual std::string GetMimeType(const std::string&) const;

 private:
  DISALLOW_COPY_AND_ASSIGN(PrintPreviewUIHTMLSource);
};

PrintPreviewUIHTMLSource::PrintPreviewUIHTMLSource()
    : DataSource(chrome::kChromeUIPrintHost, MessageLoop::current()) {
}

PrintPreviewUIHTMLSource::~PrintPreviewUIHTMLSource() {}

void PrintPreviewUIHTMLSource::StartDataRequest(const std::string& path,
                                                bool is_off_the_record,
                                                int request_id) {
  DictionaryValue localized_strings;
  SetLocalizedStrings(&localized_strings);
  SetFontAndTextDirection(&localized_strings);

  static const base::StringPiece print_html(
      ResourceBundle::GetSharedInstance().GetRawDataResource(
          IDR_PRINT_PREVIEW_HTML));
  const std::string full_html = jstemplate_builder::GetI18nTemplateHtml(
      print_html, &localized_strings);

  scoped_refptr<RefCountedBytes> html_bytes(new RefCountedBytes);
  html_bytes->data.resize(full_html.size());
  std::copy(full_html.begin(), full_html.end(), html_bytes->data.begin());

  SendResponse(request_id, html_bytes);
}

std::string PrintPreviewUIHTMLSource::GetMimeType(const std::string&) const {
  return "text/html";
}

////////////////////////////////////////////////////////////////////////////////
//
// PrintPreviewUI
//
////////////////////////////////////////////////////////////////////////////////

PrintPreviewUI::PrintPreviewUI(TabContents* contents) : DOMUI(contents) {
  // PrintPreviewUI owns |handler|.
  PrintPreviewHandler* handler = new PrintPreviewHandler();
  AddMessageHandler(handler->Attach(this));

  // Set up the chrome://print/ source.
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      NewRunnableMethod(
          Singleton<ChromeURLDataManager>::get(),
          &ChromeURLDataManager::AddDataSource,
          make_scoped_refptr(new PrintPreviewUIHTMLSource())));
}

PrintPreviewUI::~PrintPreviewUI() {
}
