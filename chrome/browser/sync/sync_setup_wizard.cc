// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/sync_setup_wizard.h"

#include "app/resource_bundle.h"
#include "base/message_loop.h"
#include "base/singleton.h"
#include "chrome/browser/browser_thread.h"
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
#include "grit/locale_settings.h"

namespace {

// Utility method to keep dictionary population code streamlined.
void AddString(DictionaryValue* dictionary,
               const std::string& key,
               int resource_id) {
  dictionary->SetString(key, l10n_util::GetStringUTF16(resource_id));
}

}

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
  using l10n_util::GetStringUTF16;
  using l10n_util::GetStringFUTF16;

  const char kSyncSetupFlowPath[] = "setup";
  const char kSyncGaiaLoginPath[] = "gaialogin";
  const char kSyncConfigurePath[] = "configure";
  const char kSyncPassphrasePath[] = "passphrase";
  const char kSyncSettingUpPath[] = "settingup";
  const char kSyncSetupDonePath[] = "setupdone";

  std::string response;
  DictionaryValue strings;
  DictionaryValue* dict = &strings;
  int html_resource_id = 0;
  if (path_raw == kSyncGaiaLoginPath) {
    html_resource_id = IDR_GAIA_LOGIN_HTML;

    // Start by setting the per-locale URLs we show on the setup wizard.
    dict->SetString("invalidpasswordhelpurl",
                    GetLocalizedUrl(kInvalidPasswordHelpUrl));
    dict->SetString("cannotaccessaccounturl",
                    GetLocalizedUrl(kCanNotAccessAccountUrl));
    dict->SetString("createnewaccounturl",
                    GetLocalizedUrl(kCreateNewAccountUrl));
    AddString(dict, "settingupsync", IDS_SYNC_LOGIN_SETTING_UP_SYNC);
    dict->SetString("introduction",
        GetStringFUTF16(IDS_SYNC_LOGIN_INTRODUCTION,
                        GetStringUTF16(IDS_PRODUCT_NAME)));
    AddString(dict, "signinprefix", IDS_SYNC_LOGIN_SIGNIN_PREFIX);
    AddString(dict, "signinsuffix", IDS_SYNC_LOGIN_SIGNIN_SUFFIX);
    AddString(dict, "cannotbeblank", IDS_SYNC_CANNOT_BE_BLANK);
    AddString(dict, "emaillabel", IDS_SYNC_LOGIN_EMAIL);
    AddString(dict, "passwordlabel", IDS_SYNC_LOGIN_PASSWORD);
    AddString(dict, "invalidcredentials", IDS_SYNC_INVALID_USER_CREDENTIALS);
    AddString(dict, "signin", IDS_SYNC_SIGNIN);
    AddString(dict, "couldnotconnect", IDS_SYNC_LOGIN_COULD_NOT_CONNECT);
    AddString(dict, "cannotaccessaccount", IDS_SYNC_CANNOT_ACCESS_ACCOUNT);
    AddString(dict, "createaccount", IDS_SYNC_CREATE_ACCOUNT);
    AddString(dict, "cancel", IDS_CANCEL);
    AddString(dict, "settingup", IDS_SYNC_LOGIN_SETTING_UP);
    AddString(dict, "success", IDS_SYNC_SUCCESS);
    AddString(dict, "errorsigningin", IDS_SYNC_ERROR_SIGNING_IN);
    AddString(dict, "captchainstructions", IDS_SYNC_GAIA_CAPTCHA_INSTRUCTIONS);
    AddString(dict, "invalidaccesscode", IDS_SYNC_INVALID_ACCESS_CODE_LABEL);
    AddString(dict, "enteraccesscode", IDS_SYNC_ENTER_ACCESS_CODE_LABEL);
    AddString(dict, "getaccesscodehelp", IDS_SYNC_ACCESS_CODE_HELP_LABEL);
    AddString(dict, "getaccesscodeurl", IDS_SYNC_GET_ACCESS_CODE_URL);
  } else if (path_raw == kSyncConfigurePath) {
    html_resource_id = IDR_SYNC_CONFIGURE_HTML;

    AddString(dict, "dataTypes", IDS_SYNC_DATA_TYPES_TAB_NAME);
    AddString(dict, "encryption", IDS_SYNC_ENCRYPTION_TAB_NAME);

    // Stuff for the choose data types localized.
    AddString(dict, "choosedatatypesheader", IDS_SYNC_CHOOSE_DATATYPES_HEADER);
    dict->SetString("choosedatatypesinstructions",
        GetStringFUTF16(IDS_SYNC_CHOOSE_DATATYPES_INSTRUCTIONS,
                        GetStringUTF16(IDS_PRODUCT_NAME)));
    AddString(dict, "keepeverythingsynced", IDS_SYNC_EVERYTHING);
    AddString(dict, "choosedatatypes", IDS_SYNC_CHOOSE_DATATYPES);
    AddString(dict, "bookmarks", IDS_SYNC_DATATYPE_BOOKMARKS);
    AddString(dict, "preferences", IDS_SYNC_DATATYPE_PREFERENCES);
    AddString(dict, "autofill", IDS_SYNC_DATATYPE_AUTOFILL);
    AddString(dict, "themes", IDS_SYNC_DATATYPE_THEMES);
    AddString(dict, "passwords", IDS_SYNC_DATATYPE_PASSWORDS);
    AddString(dict, "extensions", IDS_SYNC_DATATYPE_EXTENSIONS);
    AddString(dict, "typedurls", IDS_SYNC_DATATYPE_TYPED_URLS);
    AddString(dict, "apps", IDS_SYNC_DATATYPE_APPS);
    AddString(dict, "foreignsessions", IDS_SYNC_DATATYPE_SESSIONS);
    AddString(dict, "synczerodatatypeserror", IDS_SYNC_ZERO_DATA_TYPES_ERROR);
    AddString(dict, "abortederror", IDS_SYNC_SETUP_ABORTED_BY_PENDING_CLEAR);

    // Stuff for the encryption tab.
    dict->SetString("encryptionInstructions",
        GetStringFUTF16(IDS_SYNC_ENCRYPTION_INSTRUCTIONS,
                        GetStringUTF16(IDS_PRODUCT_NAME)));
    AddString(dict, "encryptAllLabel", IDS_SYNC_ENCRYPT_ALL_LABEL);
    AddString(dict, "usePassphraseLabel", IDS_SYNC_PASSPHRASE_CHECKBOX_LABEL);
    AddString(dict, "passphraseWarning", IDS_SYNC_PASSPHRASE_WARNING);

    // Stuff for the footer.
    AddString(dict, "ok", IDS_OK);
    AddString(dict, "cancel", IDS_CANCEL);
  } else if (path_raw == kSyncPassphrasePath) {
    html_resource_id = IDR_SYNC_PASSPHRASE_HTML;
    AddString(dict, "newPassphraseTitle", IDS_SYNC_NEW_PASSPHRASE_TITLE);
    AddString(dict, "newPassphraseBody", IDS_SYNC_NEW_PASSPHRASE_BODY);
    AddString(dict, "enterPassphraseTitle", IDS_SYNC_ENTER_PASSPHRASE_TITLE);
    AddString(dict, "enterPassphraseBody", IDS_SYNC_ENTER_PASSPHRASE_BODY);
    AddString(dict, "gaiaPassphraseTitle", IDS_SYNC_GAIA_PASSPHRASE_TITLE);
    AddString(dict, "gaiaPassphraseBody", IDS_SYNC_GAIA_PASSPHRASE_BODY);
    AddString(dict, "passphraseLabel", IDS_SYNC_PASSPHRASE_LABEL);
    AddString(dict, "ok", IDS_OK);
    AddString(dict, "cancel", IDS_CANCEL);
  } else if (path_raw == kSyncSettingUpPath) {
    html_resource_id = IDR_SYNC_SETTING_UP_HTML;

    AddString(dict, "settingup", IDS_SYNC_LOGIN_SETTING_UP);
    AddString(dict, "cancel", IDS_CANCEL);
  } else if (path_raw == kSyncSetupDonePath) {
    html_resource_id = IDR_SYNC_SETUP_DONE_HTML;

    AddString(dict, "success", IDS_SYNC_SUCCESS);
    dict->SetString("setupsummary",
                    GetStringFUTF16(IDS_SYNC_SETUP_ALL_DONE,
                                    GetStringUTF16(IDS_PRODUCT_NAME)));
    AddString(dict, "firsttimesummary", IDS_SYNC_SETUP_FIRST_TIME_ALL_DONE);
    AddString(dict, "okay", IDS_SYNC_SETUP_OK_BUTTON_LABEL);
  } else if (path_raw == kSyncSetupFlowPath) {
    html_resource_id = IDR_SYNC_SETUP_FLOW_HTML;
  }

  if (html_resource_id > 0) {
    const base::StringPiece html(
        ResourceBundle::GetSharedInstance().GetRawDataResource(
            html_resource_id));
    SetFontAndTextDirection(dict);
    response = jstemplate_builder::GetI18nTemplateHtml(html, dict);
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
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
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
  } else if (start_state == ENTER_PASSPHRASE) {
    result = DONE;
  } else if (start_state == CONFIGURE) {
    result = DONE;
  }
  DCHECK_NE(FATAL_ERROR, result) <<
      "Invalid start state for discrete run: " << start_state;
  return result;
}
