// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/sync_setup_wizard.h"

#include "app/resource_bundle.h"
#include "base/message_loop.h"
#include "base/singleton.h"
#include "chrome/browser/chrome_thread.h"
#include "chrome/browser/dom_ui/chrome_url_data_manager.h"
#include "chrome/browser/google/google_util.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/sync/sync_setup_flow.h"
#include "chrome/common/jstemplate_builder.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "googleurl/src/gurl.h"
#include "grit/app_resources.h"
#include "grit/browser_resources.h"
#include "grit/chromium_strings.h"

class SyncResourcesSource : public ChromeURLDataManager::DataSource {
 public:
  SyncResourcesSource()
      : DataSource(chrome::kChromeUISyncResourcesHost, MessageLoop::current()) {
  }

  virtual void StartDataRequest(const std::string& path,
                                bool is_off_the_record,
                                int request_id);

  virtual std::string GetMimeType(const std::string& path) const {
    return "text/html";
  }

  static const char* kInvalidPasswordHelpUrl;
  static const char* kCanNotAccessAccountUrl;
  static const char* kCreateNewAccountUrl;

 private:
  virtual ~SyncResourcesSource() {}

  // Takes a string containing an URL and returns an URL containing a CGI
  // parameter of the form "&hl=xy" where 'xy' is the language code of the
  // current locale.
  std::string GetLocalizedUrl(const std::string& url) const;

  DISALLOW_COPY_AND_ASSIGN(SyncResourcesSource);
};

const char* SyncResourcesSource::kInvalidPasswordHelpUrl =
  "http://www.google.com/support/accounts/bin/answer.py?ctx=ch&answer=27444";
const char* SyncResourcesSource::kCanNotAccessAccountUrl =
  "http://www.google.com/support/accounts/bin/answer.py?answer=48598";
const char* SyncResourcesSource::kCreateNewAccountUrl =
  "https://www.google.com/accounts/NewAccount?service=chromiumsync";

void SyncResourcesSource::StartDataRequest(const std::string& path_raw,
    bool is_off_the_record, int request_id) {
  const char kSyncGaiaLoginPath[] = "gaialogin";
  const char kSyncChooseDataTypesPath[] = "choosedatatypes";
  const char kSyncSetupFlowPath[] = "setup";
  const char kSyncSetupDonePath[] = "setupdone";

  std::string response;
  if (path_raw == kSyncGaiaLoginPath) {
    DictionaryValue localized_strings;

    // Start by setting the per-locale URLs we show on the setup wizard.
    localized_strings.SetString("invalidpasswordhelpurl",
        GetLocalizedUrl(kInvalidPasswordHelpUrl));
    localized_strings.SetString("cannotaccessaccounturl",
        GetLocalizedUrl(kCanNotAccessAccountUrl));
    localized_strings.SetString("createnewaccounturl",
        GetLocalizedUrl(kCreateNewAccountUrl));

    localized_strings.SetString("settingupsync",
        l10n_util::GetStringUTF16(IDS_SYNC_LOGIN_SETTING_UP_SYNC));
    localized_strings.SetString("introduction",
        l10n_util::GetStringFUTF16(IDS_SYNC_LOGIN_INTRODUCTION,
        l10n_util::GetStringUTF16(IDS_PRODUCT_NAME)));
    localized_strings.SetString("signinprefix",
        l10n_util::GetStringUTF16(IDS_SYNC_LOGIN_SIGNIN_PREFIX));
    localized_strings.SetString("signinsuffix",
        l10n_util::GetStringUTF16(IDS_SYNC_LOGIN_SIGNIN_SUFFIX));
    localized_strings.SetString("cannotbeblank",
        l10n_util::GetStringUTF16(IDS_SYNC_CANNOT_BE_BLANK));
    localized_strings.SetString("emaillabel",
        l10n_util::GetStringUTF16(IDS_SYNC_LOGIN_EMAIL));
    localized_strings.SetString("passwordlabel",
        l10n_util::GetStringUTF16(IDS_SYNC_LOGIN_PASSWORD));
    localized_strings.SetString("invalidcredentials",
        l10n_util::GetStringUTF16(IDS_SYNC_INVALID_USER_CREDENTIALS));
    localized_strings.SetString("signin",
        l10n_util::GetStringUTF16(IDS_SYNC_SIGNIN));
    localized_strings.SetString("couldnotconnect",
        l10n_util::GetStringUTF16(IDS_SYNC_LOGIN_COULD_NOT_CONNECT));
    localized_strings.SetString("cannotaccessaccount",
        l10n_util::GetStringUTF16(IDS_SYNC_CANNOT_ACCESS_ACCOUNT));
    localized_strings.SetString("createaccount",
        l10n_util::GetStringUTF16(IDS_SYNC_CREATE_ACCOUNT));
    localized_strings.SetString("cancel",
        l10n_util::GetStringUTF16(IDS_CANCEL));
    localized_strings.SetString("settingup",
        l10n_util::GetStringUTF16(IDS_SYNC_LOGIN_SETTING_UP));
    localized_strings.SetString("success",
        l10n_util::GetStringUTF16(IDS_SYNC_SUCCESS));
    localized_strings.SetString("errorsigningin",
        l10n_util::GetStringUTF16(IDS_SYNC_ERROR_SIGNING_IN));
    localized_strings.SetString("captchainstructions",
        l10n_util::GetStringUTF16(IDS_SYNC_GAIA_CAPTCHA_INSTRUCTIONS));
    static const base::StringPiece html(ResourceBundle::GetSharedInstance()
        .GetRawDataResource(IDR_GAIA_LOGIN_HTML));
    SetFontAndTextDirection(&localized_strings);
    response = jstemplate_builder::GetI18nTemplateHtml(
        html, &localized_strings);
  } else if (path_raw == kSyncChooseDataTypesPath) {
    DictionaryValue localized_strings;
    localized_strings.SetString("choosedatatypesheader",
        l10n_util::GetStringUTF16(IDS_SYNC_CHOOSE_DATATYPES_HEADER));
    localized_strings.SetString("choosedatatypesinstructions",
        l10n_util::GetStringFUTF16(IDS_SYNC_CHOOSE_DATATYPES_INSTRUCTIONS,
        l10n_util::GetStringUTF16(IDS_PRODUCT_NAME)));
    localized_strings.SetString("keepeverythingsynced",
        l10n_util::GetStringUTF16(IDS_SYNC_EVERYTHING));
    localized_strings.SetString("choosedatatypes",
        l10n_util::GetStringUTF16(IDS_SYNC_CHOOSE_DATATYPES));
    localized_strings.SetString("bookmarks",
        l10n_util::GetStringUTF16(IDS_SYNC_DATATYPE_BOOKMARKS));
    localized_strings.SetString("preferences",
        l10n_util::GetStringUTF16(IDS_SYNC_DATATYPE_PREFERENCES));
    localized_strings.SetString("autofill",
        l10n_util::GetStringUTF16(IDS_SYNC_DATATYPE_AUTOFILL));
    localized_strings.SetString("themes",
        l10n_util::GetStringUTF16(IDS_SYNC_DATATYPE_THEMES));
    localized_strings.SetString("passwords",
        l10n_util::GetStringUTF16(IDS_SYNC_DATATYPE_PASSWORDS));
    localized_strings.SetString("extensions",
        l10n_util::GetStringUTF16(IDS_SYNC_DATATYPE_EXTENSIONS));
    localized_strings.SetString("typedurls",
        l10n_util::GetStringUTF16(IDS_SYNC_DATATYPE_TYPED_URLS));
    localized_strings.SetString("apps",
        l10n_util::GetStringUTF16(IDS_SYNC_DATATYPE_APPS));
    localized_strings.SetString("synczerodatatypeserror",
        l10n_util::GetStringUTF16(IDS_SYNC_ZERO_DATA_TYPES_ERROR));
    localized_strings.SetString("setupabortederror",
        l10n_util::GetStringUTF16(IDS_SYNC_SETUP_ABORTED_BY_PENDING_CLEAR));
    localized_strings.SetString("ok",
        l10n_util::GetStringUTF16(IDS_OK));
    localized_strings.SetString("cancel",
        l10n_util::GetStringUTF16(IDS_CANCEL));
    localized_strings.SetString("settingup",
        l10n_util::GetStringUTF16(IDS_SYNC_LOGIN_SETTING_UP));
    static const base::StringPiece html(ResourceBundle::GetSharedInstance()
      .GetRawDataResource(IDR_SYNC_CHOOSE_DATATYPES_HTML));
    SetFontAndTextDirection(&localized_strings);
    response = jstemplate_builder::GetI18nTemplateHtml(
      html, &localized_strings);
  } else if (path_raw == kSyncSetupDonePath) {
    DictionaryValue localized_strings;
    localized_strings.SetString("success",
        l10n_util::GetStringUTF16(IDS_SYNC_SUCCESS));
    localized_strings.SetString("setupsummary",
        l10n_util::GetStringFUTF16(IDS_SYNC_SETUP_ALL_DONE,
        l10n_util::GetStringUTF16(IDS_PRODUCT_NAME)));
    localized_strings.SetString("firsttimesetupsummary",
        l10n_util::GetStringUTF16(IDS_SYNC_SETUP_FIRST_TIME_ALL_DONE));
    localized_strings.SetString("okay",
        l10n_util::GetStringUTF16(IDS_SYNC_SETUP_OK_BUTTON_LABEL));
    static const base::StringPiece html(ResourceBundle::GetSharedInstance()
        .GetRawDataResource(IDR_SYNC_SETUP_DONE_HTML));
    SetFontAndTextDirection(&localized_strings);
    response = jstemplate_builder::GetI18nTemplateHtml(
        html, &localized_strings);
  } else if (path_raw == kSyncSetupFlowPath) {
    static const base::StringPiece html(ResourceBundle::GetSharedInstance()
        .GetRawDataResource(IDR_SYNC_SETUP_FLOW_HTML));
    response = html.as_string();
  }
  // Send the response.
  scoped_refptr<RefCountedBytes> html_bytes(new RefCountedBytes);
  html_bytes->data.resize(response.size());
  std::copy(response.begin(), response.end(), html_bytes->data.begin());
  SendResponse(request_id, html_bytes);
}

std::string SyncResourcesSource::GetLocalizedUrl(
    const std::string& url) const {
  GURL original_url(url);
  DCHECK(original_url.is_valid());
  GURL localized_url = google_util::AppendGoogleLocaleParam(original_url);
  return localized_url.spec();
}

SyncSetupWizard::SyncSetupWizard(ProfileSyncService* service)
    : service_(service),
      flow_container_(new SyncSetupFlowContainer()),
      parent_window_(NULL) {
  // Add our network layer data source for 'cloudy' URLs.
  SyncResourcesSource* sync_source = new SyncResourcesSource();
  ChromeThread::PostTask(
      ChromeThread::IO, FROM_HERE,
      NewRunnableMethod(Singleton<ChromeURLDataManager>::get(),
                        &ChromeURLDataManager::AddDataSource,
                        make_scoped_refptr(sync_source)));
}

SyncSetupWizard::~SyncSetupWizard() {
  delete flow_container_;
}

void SyncSetupWizard::Step(State advance_state) {
  SyncSetupFlow* flow = flow_container_->get_flow();
  if (flow) {
    // A setup flow is in progress and dialog is currently showing.
    flow->Advance(advance_state);
  } else if (!service_->profile()->GetPrefs()->GetBoolean(
             prefs::kSyncHasSetupCompleted)) {
    if (IsTerminalState(advance_state))
      return;
    // No flow is in progress, and we have never escorted the user all the
    // way through the wizard flow.
    flow_container_->set_flow(
        SyncSetupFlow::Run(service_, flow_container_, advance_state, DONE,
                           parent_window_));
  } else {
    // No flow in in progress, but we've finished the wizard flow once before.
    // This is just a discrete run.
    if (IsTerminalState(advance_state))
      return;  // Nothing to do.
    flow_container_->set_flow(SyncSetupFlow::Run(service_, flow_container_,
        advance_state, GetEndStateForDiscreteRun(advance_state),
        parent_window_));
  }
}

// static
bool SyncSetupWizard::IsTerminalState(State advance_state) {
  return advance_state == GAIA_SUCCESS ||
         advance_state == DONE ||
         advance_state == DONE_FIRST_TIME ||
         advance_state == FATAL_ERROR ||
         advance_state == SETUP_ABORTED_BY_PENDING_CLEAR;
}

bool SyncSetupWizard::IsVisible() const {
  return flow_container_->get_flow() != NULL;
}

void SyncSetupWizard::Focus() {
  SyncSetupFlow* flow = flow_container_->get_flow();
  if (flow) {
    flow->Focus();
  }
}

void SyncSetupWizard::SetParent(gfx::NativeWindow parent_window) {
  parent_window_ = parent_window;
}

// static
SyncSetupWizard::State SyncSetupWizard::GetEndStateForDiscreteRun(
    State start_state) {
  State result = FATAL_ERROR;
  if (start_state == GAIA_LOGIN) {
    result = GAIA_SUCCESS;
  } else if (start_state == CHOOSE_DATA_TYPES) {
    result = DONE;
  }
  DCHECK_NE(FATAL_ERROR, result) <<
      "Invalid start state for discrete run: " << start_state;
  return result;
}
