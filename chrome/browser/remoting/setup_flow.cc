// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/remoting/setup_flow.h"

#include "app/gfx/font_util.h"
#include "app/l10n_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_thread.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/remoting/remoting_resources_source.h"
#include "chrome/browser/remoting/setup_flow_login_step.h"
#include "chrome/browser/renderer_host/render_view_host.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/common/pref_names.h"
#include "gfx/font.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"

namespace remoting {

static const wchar_t kDoneIframeXPath[] = L"//iframe[@id='done']";
static const wchar_t kErrorIframeXPath[] = L"//iframe[@id='error']";

SetupFlowStep::SetupFlowStep() { }
SetupFlowStep::~SetupFlowStep() { }

SetupFlowStepBase::SetupFlowStepBase()
    : flow_(NULL) {
}

SetupFlowStepBase::~SetupFlowStepBase() { }

void SetupFlowStepBase::Start(SetupFlow* flow, DoneCallback* done_callback) {
  done_callback_.reset(done_callback);
  flow_ = flow;
  DoStart();
}

SetupFlowStep* SetupFlowStepBase::GetNextStep() {
  DCHECK(done_);
  return next_step_;
}

void SetupFlowStepBase::ExecuteJavascriptInIFrame(
    const std::wstring& iframe_xpath, const std::wstring& js) {
  DOMUI* dom_ui = flow()->dom_ui();
  DCHECK(dom_ui);

  RenderViewHost* rvh = dom_ui->tab_contents()->render_view_host();
  rvh->ExecuteJavascriptInWebFrame(iframe_xpath, js);
}

void SetupFlowStepBase::FinishStep(SetupFlowStep* next_step) {
  next_step_ = next_step;
  done_ = true;
  done_callback_->Run();
}

SetupFlowErrorStepBase::SetupFlowErrorStepBase() { }
SetupFlowErrorStepBase::~SetupFlowErrorStepBase() { }

void SetupFlowErrorStepBase::HandleMessage(const std::string& message,
                                           const Value* arg) {
  if (message == "Retry") {
    Retry();
  }
}

void SetupFlowErrorStepBase::Cancel() { }

void SetupFlowErrorStepBase::DoStart() {
  std::wstring javascript =
      L"setMessage('" + UTF16ToWide(GetErrorMessage()) + L"');";
  ExecuteJavascriptInIFrame(kErrorIframeXPath, javascript);

  flow()->dom_ui()->CallJavascriptFunction(L"showError");

  ExecuteJavascriptInIFrame(kErrorIframeXPath, L"onPageShown();");
}

SetupFlowDoneStep::SetupFlowDoneStep() {
  message_ = l10n_util::GetStringUTF16(IDS_REMOTING_SUCCESS_MESSAGE);
}

SetupFlowDoneStep::SetupFlowDoneStep(const string16& message)
    : message_(message) {
}

SetupFlowDoneStep::~SetupFlowDoneStep() { }

void SetupFlowDoneStep::HandleMessage(const std::string& message,
                                      const Value* arg) {
}

void SetupFlowDoneStep::Cancel() { }

void SetupFlowDoneStep::DoStart() {
  std::wstring javascript =
      L"setMessage('" + UTF16ToWide(message_) + L"');";
  ExecuteJavascriptInIFrame(kDoneIframeXPath, javascript);

  flow()->dom_ui()->CallJavascriptFunction(L"showSetupDone");

  ExecuteJavascriptInIFrame(kDoneIframeXPath, L"onPageShown();");
}

SetupFlowContext::SetupFlowContext() { }
SetupFlowContext::~SetupFlowContext() { }

SetupFlow::SetupFlow(const std::string& args, Profile* profile,
                     SetupFlowStep* first_step)
    : dom_ui_(NULL),
      dialog_start_args_(args),
      profile_(profile),
      current_step_(first_step) {
  // TODO(hclam): The data source should be added once.
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      NewRunnableMethod(ChromeURLDataManager::GetInstance(),
                        &ChromeURLDataManager::AddDataSource,
                        make_scoped_refptr(new RemotingResourcesSource())));
}

SetupFlow::~SetupFlow() { }

// static
SetupFlow* SetupFlow::OpenSetupDialog(Profile* profile) {
  // Set the arguments for showing the gaia login page.
  DictionaryValue args;
  args.SetString("iframeToShow", "login");
  args.SetString("user", "");
  args.SetInteger("error", 0);
  args.SetBoolean("editable_user", true);

  std::string json_args;
  base::JSONWriter::Write(&args, false, &json_args);

  Browser* b = BrowserList::GetLastActive();
  if (!b)
    return NULL;

  SetupFlow *flow = new SetupFlow(json_args, profile, new SetupFlowLoginStep());
  b->BrowserShowHtmlDialog(flow, NULL);
  return flow;
}

GURL SetupFlow::GetDialogContentURL() const {
  return GURL("chrome://remotingresources/setup");
}

void SetupFlow::GetDOMMessageHandlers(
    std::vector<DOMMessageHandler*>* handlers) const {
  // The called will be responsible for deleting this object.
  handlers->push_back(const_cast<SetupFlow*>(this));
}

void SetupFlow::GetDialogSize(gfx::Size* size) const {
  PrefService* prefs = profile_->GetPrefs();
  gfx::Font approximate_web_font(
      UTF8ToWide(prefs->GetString(prefs::kWebKitSansSerifFontFamily)),
      prefs->GetInteger(prefs::kWebKitDefaultFontSize));

  // TODO(pranavk) Replace the following SYNC resources with REMOTING Resources.
  *size = gfx::GetLocalizedContentsSizeForFont(
      IDS_SYNC_SETUP_WIZARD_WIDTH_CHARS,
      IDS_SYNC_SETUP_WIZARD_HEIGHT_LINES,
      approximate_web_font);
}

// A callback to notify the delegate that the dialog closed.
void SetupFlow::OnDialogClosed(const std::string& json_retval) {
  if (current_step_ != NULL)
    current_step_->Cancel();
}

std::string SetupFlow::GetDialogArgs() const {
    return dialog_start_args_;
}

void SetupFlow::OnCloseContents(TabContents* source,
                                bool* out_close_dialog) {
}

std::wstring SetupFlow::GetDialogTitle() const {
  return UTF16ToWideHack(
      l10n_util::GetStringUTF16(IDS_REMOTING_SETUP_DIALOG_TITLE));
}

bool SetupFlow::IsDialogModal() const {
  return false;
}

bool SetupFlow::ShouldShowDialogTitle() const {
  return true;
}

DOMMessageHandler* SetupFlow::Attach(DOMUI* dom_ui) {
  dom_ui_ = dom_ui;
  StartCurrentStep();
  return DOMMessageHandler::Attach(dom_ui);
}

void SetupFlow::RegisterMessages() {
  dom_ui_->RegisterMessageCallback(
      "SubmitAuth", NewCallback(this, &SetupFlow::HandleSubmitAuth));
  dom_ui_->RegisterMessageCallback(
      "RemotingSetup", NewCallback(this, &SetupFlow::HandleUIMessage));
}

void SetupFlow::HandleSubmitAuth(const ListValue* args) {
  Value* first_arg = NULL;
  if (!args->Get(0, &first_arg)) {
    NOTREACHED();
    return;
  }

  current_step_->HandleMessage("SubmitAuth", first_arg);
}

void SetupFlow::HandleUIMessage(const ListValue* args) {
  std::string message;
  Value* message_value;
  if (!args->Get(0, &message_value) ||
      !message_value->GetAsString(&message)) {
    NOTREACHED();
    return;
  }

  // Message argument is optional and set to NULL if it is not
  // provided by the sending page.
  Value* arg_value = NULL;
  if (args->GetSize() >= 2) {
    if (!args->Get(1, &arg_value)) {
      NOTREACHED();
      return;
    }
  }

  current_step_->HandleMessage(message, arg_value);
}

void SetupFlow::StartCurrentStep() {
  current_step_->Start(this, NewCallback(this, &SetupFlow::OnStepDone));
}

void SetupFlow::OnStepDone() {
  SetupFlowStep* next_step = current_step_->GetNextStep();

  if (current_step_.get()) {
    // Can't destroy current step here. Schedule it to be destroyed later.
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        new DeleteTask<SetupFlowStep>(current_step_.release()));
  }

  current_step_.reset(next_step);
  StartCurrentStep();
}

}  // namespace remoting
