// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/printing/print_dialog_cloud.h"
#include "chrome/browser/printing/print_dialog_cloud_internal.h"

#include "app/l10n_util.h"
#include "base/base64.h"
#include "base/file_util.h"
#include "base/json/json_reader.h"
#include "base/values.h"
#include "chrome/browser/browser_list.h"
#include "chrome/browser/chrome_thread.h"
#include "chrome/browser/debugger/devtools_manager.h"
#include "chrome/browser/dom_ui/dom_ui.h"
#include "chrome/browser/dom_ui/dom_ui_util.h"
#include "chrome/browser/dom_ui/html_dialog_ui.h"
#include "chrome/browser/printing/cloud_print/cloud_print_url.h"
#include "chrome/browser/renderer_host/render_view_host.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/common/notification_observer.h"
#include "chrome/common/notification_registrar.h"
#include "chrome/common/notification_source.h"
#include "chrome/common/notification_type.h"
#include "chrome/common/render_messages_params.h"
#include "chrome/common/url_constants.h"
#include "webkit/glue/webpreferences.h"

#include "grit/generated_resources.h"

// This module implements the UI support in Chrome for cloud printing.
// This means hosting a dialog containing HTML/JavaScript and using
// the published cloud print user interface integration APIs to get
// page setup settings from the dialog contents and provide the
// generated print PDF to the dialog contents for uploading to the
// cloud print service.

// Currently, the flow between these classes is as follows:

// PrintDialogCloud::CreatePrintDialogForPdf is called from
// resource_message_filter_gtk.cc once the renderer has informed the
// renderer host that PDF generation into the renderer host provided
// temp file has been completed.  That call is on the IO thread.
// That, in turn, hops over to the UI thread to create an instance of
// PrintDialogCloud.

// The constructor for PrintDialogCloud creates a
// CloudPrintHtmlDialogDelegate and asks the current active browser to
// show an HTML dialog using that class as the delegate. That class
// hands in the kCloudPrintResourcesURL as the URL to visit.  That is
// recognized by the GetDOMUIFactoryFunction as a signal to create an
// ExternalHtmlDialogUI.

// CloudPrintHtmlDialogDelegate also temporarily owns a
// CloudPrintFlowHandler, a class which is responsible for the actual
// interactions with the dialog contents, including handing in the PDF
// print data and getting any page setup parameters that the dialog
// contents provides.  As part of bringing up the dialog,
// HtmlDialogUI::RenderViewCreated is called (an override of
// DOMUI::RenderViewCreated).  That routine, in turn, calls the
// delegate's GetDOMMessageHandlers routine, at which point the
// ownership of the CloudPrintFlowHandler is handed over.  A pointer
// to the flow handler is kept to facilitate communication back and
// forth between the two classes.

// The DOMUI continues dialog bring-up, calling
// CloudPrintFlowHandler::RegisterMessages.  This is where the
// additional object model capabilities are registered for the dialog
// contents to use.  It is also at this time that capabilities for the
// dialog contents are adjusted to allow the dialog contents to close
// the window.  In addition, the pending URL is redirected to the
// actual cloud print service URL.  The flow controller also registers
// for notification of when the dialog contents finish loading, which
// is currently used to send the PDF data to the dialog contents.

// In order to send the PDF data to the dialog contents, the flow
// handler uses a CloudPrintDataSender.  It creates one, letting it
// know the name of the temporary file containing the PDF data, and
// posts the task of reading the file
// (CloudPrintDataSender::ReadPrintDataFile) to the file thread.  That
// routine reads in the file, and then hops over to the IO thread to
// send that data to the dialog contents.

// When the dialog contents are finished (by either being cancelled or
// hitting the print button), the delegate is notified, and responds
// that the dialog should be closed, at which point things are torn
// down and released.

// TODO(scottbyer):
// http://code.google.com/p/chromium/issues/detail?id=44093 The
// high-level flow (where the PDF data is generated before even
// bringing up the dialog) isn't what we want.


namespace internal_cloud_print_helpers {

bool GetRealOrInt(const DictionaryValue& dictionary,
                  const std::string& path,
                  double* out_value) {
  if (!dictionary.GetReal(path, out_value)) {
    int int_value = 0;
    if (!dictionary.GetInteger(path, &int_value))
      return false;
    *out_value = int_value;
  }
  return true;
}

// From the JSON parsed value, get the entries for the page setup
// parameters.
bool GetPageSetupParameters(const std::string& json,
                            ViewMsg_Print_Params& parameters) {
  scoped_ptr<Value> parsed_value(base::JSONReader::Read(json, false));
  DLOG_IF(ERROR, (!parsed_value.get() ||
                  !parsed_value->IsType(Value::TYPE_DICTIONARY)))
      << "PageSetup call didn't have expected contents";
  if (!parsed_value.get() || !parsed_value->IsType(Value::TYPE_DICTIONARY))
    return false;

  bool result = true;
  DictionaryValue* params = static_cast<DictionaryValue*>(parsed_value.get());
  result &= GetRealOrInt(*params, "dpi", &parameters.dpi);
  result &= GetRealOrInt(*params, "min_shrink", &parameters.min_shrink);
  result &= GetRealOrInt(*params, "max_shrink", &parameters.max_shrink);
  result &= params->GetBoolean("selection_only", &parameters.selection_only);
  return result;
}

void CloudPrintDataSenderHelper::CallJavascriptFunction(
    const std::wstring& function_name) {
  dom_ui_->CallJavascriptFunction(function_name);
}

void CloudPrintDataSenderHelper::CallJavascriptFunction(
    const std::wstring& function_name, const Value& arg) {
  dom_ui_->CallJavascriptFunction(function_name, arg);
}

void CloudPrintDataSenderHelper::CallJavascriptFunction(
    const std::wstring& function_name, const Value& arg1, const Value& arg2) {
  dom_ui_->CallJavascriptFunction(function_name, arg1, arg2);
}

// Clears out the pointer we're using to communicate.  Either routine is
// potentially expensive enough that stopping whatever is in progress
// is worth it.
void CloudPrintDataSender::CancelPrintDataFile() {
  AutoLock lock(lock_);
  // We don't own helper, it was passed in to us, so no need to
  // delete, just let it go.
  helper_ = NULL;
}

// Grab the raw PDF file contents and massage them into shape for
// sending to the dialog contents (and up to the cloud print server)
// by encoding it and prefixing it with the appropriate mime type.
// Once that is done, kick off the next part of the task on the IO
// thread.
void CloudPrintDataSender::ReadPrintDataFile(const FilePath& path_to_pdf) {
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::FILE));
  int64 file_size = 0;
  if (file_util::GetFileSize(path_to_pdf, &file_size) && file_size != 0) {
    std::string file_data;
    if (file_size < kuint32max) {
      file_data.reserve(static_cast<unsigned int>(file_size));
    } else {
      DLOG(WARNING) << " print data file too large to reserve space";
    }
    if (helper_ && file_util::ReadFileToString(path_to_pdf, &file_data)) {
      std::string base64_data;
      base::Base64Encode(file_data, &base64_data);
      std::string header("data:application/pdf;base64,");
      base64_data.insert(0, header);
      scoped_ptr<StringValue> new_data(new StringValue(base64_data));
      print_data_.swap(new_data);
      ChromeThread::PostTask(ChromeThread::IO, FROM_HERE,
                             NewRunnableMethod(
                                 this,
                                 &CloudPrintDataSender::SendPrintDataFile));
    }
  }
}

// We have the data in hand that needs to be pushed into the dialog
// contents; do so from the IO thread.

// TODO(scottbyer): If the print data ends up being larger than the
// upload limit (currently 10MB), what we need to do is upload that
// large data to google docs and set the URL in the printing
// JavaScript to that location, and make sure it gets deleted when not
// needed. - 4/1/2010
void CloudPrintDataSender::SendPrintDataFile() {
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::IO));
  AutoLock lock(lock_);
  if (helper_ && print_data_.get()) {
    StringValue title(print_job_title_);

    // Send the print data to the dialog contents.  The JavaScript
    // function is a preliminary API for prototyping purposes and is
    // subject to change.
    const_cast<CloudPrintDataSenderHelper*>(helper_)->CallJavascriptFunction(
        L"printApp._printDataUrl", *print_data_, title);
  }
}


void CloudPrintFlowHandler::SetDialogDelegate(
    CloudPrintHtmlDialogDelegate* delegate) {
  // Even if setting a new dom_ui, it means any previous task needs
  // to be cancelled, it's now invalid.
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::UI));
  CancelAnyRunningTask();
  dialog_delegate_ = delegate;
}

// Cancels any print data sender we have in flight and removes our
// reference to it, so when the task that is calling it finishes and
// removes it's reference, it goes away.
void CloudPrintFlowHandler::CancelAnyRunningTask() {
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::UI));
  if (print_data_sender_.get()) {
    print_data_sender_->CancelPrintDataFile();
    print_data_sender_ = NULL;
  }
}


void CloudPrintFlowHandler::RegisterMessages() {
  if (!dom_ui_)
    return;

  // TODO(scottbyer) - This is where we will register messages for the
  // UI JS to use.  Needed: Call to update page setup parameters.
  dom_ui_->RegisterMessageCallback(
      "ShowDebugger",
      NewCallback(this, &CloudPrintFlowHandler::HandleShowDebugger));
  dom_ui_->RegisterMessageCallback(
      "SendPrintData",
      NewCallback(this, &CloudPrintFlowHandler::HandleSendPrintData));
  dom_ui_->RegisterMessageCallback(
      "SetPageParameters",
      NewCallback(this, &CloudPrintFlowHandler::HandleSetPageParameters));

  if (dom_ui_->tab_contents()) {
    // Also, take the opportunity to set some (minimal) additional
    // script permissions required for the web UI.

    // TODO(scottbyer): learn how to make sure we're talking to the
    // right web site first.
    RenderViewHost* rvh = dom_ui_->tab_contents()->render_view_host();
    if (rvh && rvh->delegate()) {
      WebPreferences webkit_prefs = rvh->delegate()->GetWebkitPrefs();
      webkit_prefs.allow_scripts_to_close_windows = true;
      rvh->UpdateWebPreferences(webkit_prefs);
    }

    // Register for appropriate notifications, and re-direct the URL
    // to the real server URL, now that we've gotten an HTML dialog
    // going.
    NavigationController* controller = &dom_ui_->tab_contents()->controller();
    NavigationEntry* pending_entry = controller->pending_entry();
    if (pending_entry)
      pending_entry->set_url(CloudPrintURL(
          dom_ui_->GetProfile()).GetCloudPrintServiceDialogURL());
    registrar_.Add(this, NotificationType::LOAD_STOP,
                   Source<NavigationController>(controller));
  }
}

void CloudPrintFlowHandler::Observe(NotificationType type,
                                    const NotificationSource& source,
                                    const NotificationDetails& details) {
  if (type == NotificationType::LOAD_STOP) {
    // Choose one or the other.  If you need to debug, bring up the
    // debugger.  You can then use the various chrome.send()
    // registrations above to kick of the various function calls,
    // including chrome.send("SendPrintData") in the javaScript
    // console and watch things happen with:
    // HandleShowDebugger(NULL);
    HandleSendPrintData(NULL);
  }
}

void CloudPrintFlowHandler::HandleShowDebugger(const ListValue* args) {
  ShowDebugger();
}

void CloudPrintFlowHandler::ShowDebugger() {
  if (dom_ui_) {
    RenderViewHost* rvh = dom_ui_->tab_contents()->render_view_host();
    if (rvh)
      DevToolsManager::GetInstance()->OpenDevToolsWindow(rvh);
  }
}

scoped_refptr<CloudPrintDataSender>
CloudPrintFlowHandler::CreateCloudPrintDataSender() {
  DCHECK(dom_ui_);
  print_data_helper_.reset(new CloudPrintDataSenderHelper(dom_ui_));
  return new CloudPrintDataSender(print_data_helper_.get(), print_job_title_);
}

void CloudPrintFlowHandler::HandleSendPrintData(const ListValue* args) {
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::UI));
  // This will cancel any ReadPrintDataFile() or SendPrintDataFile()
  // requests in flight (this is anticipation of when setting page
  // setup parameters becomes asynchronous and may be set while some
  // data is in flight).  Then we can clear out the print data.
  CancelAnyRunningTask();
  if (dom_ui_) {
    print_data_sender_ = CreateCloudPrintDataSender();
    ChromeThread::PostTask(ChromeThread::FILE, FROM_HERE,
                           NewRunnableMethod(
                               print_data_sender_.get(),
                               &CloudPrintDataSender::ReadPrintDataFile,
                               path_to_pdf_));
  }
}

void CloudPrintFlowHandler::HandleSetPageParameters(const ListValue* args) {
  std::string json(dom_ui_util::GetJsonResponseFromFirstArgumentInList(args));
  if (json.empty())
    return;

  // These are backstop default values - 72 dpi to match the screen,
  // 8.5x11 inch paper with margins subtracted (1/4 inch top, left,
  // right and 0.56 bottom), and the min page shrink and max page
  // shrink values appear all over the place with no explanation.

  // TODO(scottbyer): Get a Linux/ChromeOS edge for PrintSettings
  // working so that we can get the default values from there.  Fix up
  // PrintWebViewHelper to do the same.
  const int kDPI = 72;
  const int kWidth = static_cast<int>((8.5-0.25-0.25)*kDPI);
  const int kHeight = static_cast<int>((11-0.25-0.56)*kDPI);
  const double kMinPageShrink = 1.25;
  const double kMaxPageShrink = 2.0;

  ViewMsg_Print_Params default_settings;
  default_settings.printable_size = gfx::Size(kWidth, kHeight);
  default_settings.dpi = kDPI;
  default_settings.min_shrink = kMinPageShrink;
  default_settings.max_shrink = kMaxPageShrink;
  default_settings.desired_dpi = kDPI;
  default_settings.document_cookie = 0;
  default_settings.selection_only = false;

  if (!GetPageSetupParameters(json, default_settings)) {
    NOTREACHED();
    return;
  }

  // TODO(scottbyer) - Here is where we would kick the originating
  // renderer thread with these new parameters in order to get it to
  // re-generate the PDF and hand it back to us.  window.print() is
  // currently synchronous, so there's a lot of work to do to get to
  // that point.
}

CloudPrintHtmlDialogDelegate::CloudPrintHtmlDialogDelegate(
    const FilePath& path_to_pdf,
    int width, int height,
    const std::string& json_arguments,
    const string16& print_job_title)
    : flow_handler_(new CloudPrintFlowHandler(path_to_pdf, print_job_title)),
      owns_flow_handler_(true) {
  Init(width, height, json_arguments);
}

CloudPrintHtmlDialogDelegate::CloudPrintHtmlDialogDelegate(
    CloudPrintFlowHandler* flow_handler,
    int width, int height,
    const std::string& json_arguments)
    : flow_handler_(flow_handler),
      owns_flow_handler_(true) {
  Init(width, height, json_arguments);
}

void CloudPrintHtmlDialogDelegate::Init(
    int width, int height, const std::string& json_arguments) {
  // This information is needed to show the dialog HTML content.
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::UI));
  std::string cloud_print_url(chrome::kCloudPrintResourcesURL);
  params_.url = GURL(cloud_print_url);
  params_.height = height;
  params_.width = width;
  params_.json_input = json_arguments;

  flow_handler_->SetDialogDelegate(this);
}

CloudPrintHtmlDialogDelegate::~CloudPrintHtmlDialogDelegate() {
  // If the flow_handler_ is about to outlive us because we don't own
  // it anymore, we need to have it remove it's reference to us.
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::UI));
  flow_handler_->SetDialogDelegate(NULL);
  if (owns_flow_handler_) {
    delete flow_handler_;
  }
}

bool CloudPrintHtmlDialogDelegate::IsDialogModal() const {
  return true;
}

std::wstring CloudPrintHtmlDialogDelegate::GetDialogTitle() const {
  return l10n_util::GetString(IDS_CLOUD_PRINT_TITLE);
}

GURL CloudPrintHtmlDialogDelegate::GetDialogContentURL() const {
  return params_.url;
}

void CloudPrintHtmlDialogDelegate::GetDOMMessageHandlers(
    std::vector<DOMMessageHandler*>* handlers) const {
  handlers->push_back(flow_handler_);
  // We don't own flow_handler_ anymore, but it sticks around until at
  // least right after OnDialogClosed() is called (and this object is
  // destroyed).
  owns_flow_handler_ = false;
}

void CloudPrintHtmlDialogDelegate::GetDialogSize(gfx::Size* size) const {
  size->set_width(params_.width);
  size->set_height(params_.height);
}

std::string CloudPrintHtmlDialogDelegate::GetDialogArgs() const {
  return params_.json_input;
}

void CloudPrintHtmlDialogDelegate::OnDialogClosed(
    const std::string& json_retval) {
  delete this;
}

void CloudPrintHtmlDialogDelegate::OnCloseContents(TabContents* source,
                                                   bool* out_close_dialog) {
  if (out_close_dialog)
    *out_close_dialog = true;
}

}  // end of namespace internal_cloud_print_helpers

// static, called on the IO thread.  This is the main entry point into
// creating the dialog.

// TODO(scottbyer): The signature here will need to change as the
// workflow through the printing code changes to allow for dynamically
// changing page setup parameters while the dialog is active.
void PrintDialogCloud::CreatePrintDialogForPdf(const FilePath& path_to_pdf) {
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::IO));

  ChromeThread::PostTask(
      ChromeThread::UI, FROM_HERE,
      NewRunnableFunction(&PrintDialogCloud::CreateDialogImpl, path_to_pdf));
}

// static, called from the UI thread.
void PrintDialogCloud::CreateDialogImpl(const FilePath& path_to_pdf) {
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::UI));
  new PrintDialogCloud(path_to_pdf);
}

// Initialize the print dialog.  Called on the UI thread.
PrintDialogCloud::PrintDialogCloud(const FilePath& path_to_pdf)
    : browser_(BrowserList::GetLastActive()) {

  // TODO(scottbyer): Verify GAIA login valid, execute GAIA login if not (should
  // be distilled out of bookmark sync.)
  string16 print_job_title;
  if (browser_ && browser_->GetSelectedTabContents())
    print_job_title = browser_->GetSelectedTabContents()->GetTitle();

  // TODO(scottbyer): Get the dialog width, height from the dialog
  // contents, and take the screen size into account.
  HtmlDialogUIDelegate* dialog_delegate =
      new internal_cloud_print_helpers::CloudPrintHtmlDialogDelegate(
          path_to_pdf, 500, 400, std::string(), print_job_title);
  browser_->BrowserShowHtmlDialog(dialog_delegate, NULL);
}

PrintDialogCloud::~PrintDialogCloud() {
}
