// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync/sync_setup_flow.h"

#include "app/gfx/font_util.h"
#include "base/callback.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/metrics/histogram.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#if defined(OS_MACOSX)
#include "chrome/browser/cocoa/html_dialog_window_controller_cppsafe.h"
#endif
#include "chrome/browser/dom_ui/dom_ui_util.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/renderer_host/render_view_host.h"
#include "chrome/browser/sync/profile_sync_service.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/common/net/gaia/google_service_auth_error.h"
#include "chrome/common/pref_names.h"
#include "gfx/font.h"
#include "grit/locale_settings.h"

// XPath expression for finding specific iframes.
static const wchar_t* kLoginIFrameXPath = L"//iframe[@id='login']";
static const wchar_t* kChooseDataTypesIFrameXPath =
    L"//iframe[@id='configure']";
static const wchar_t* kPassphraseIFrameXPath =
    L"//iframe[@id='passphrase']";
static const wchar_t* kDoneIframeXPath = L"//iframe[@id='done']";

void FlowHandler::RegisterMessages() {
  dom_ui_->RegisterMessageCallback("SubmitAuth",
      NewCallback(this, &FlowHandler::HandleSubmitAuth));
  dom_ui_->RegisterMessageCallback("Configure",
      NewCallback(this, &FlowHandler::HandleConfigure));
  dom_ui_->RegisterMessageCallback("Passphrase",
      NewCallback(this, &FlowHandler::HandlePassphraseEntry));
}

static bool GetAuthData(const std::string& json,
                        std::string* username,
                        std::string* password,
                        std::string* captcha,
                        std::string* access_code) {
  scoped_ptr<Value> parsed_value(base::JSONReader::Read(json, false));
  if (!parsed_value.get() || !parsed_value->IsType(Value::TYPE_DICTIONARY))
    return false;

  DictionaryValue* result = static_cast<DictionaryValue*>(parsed_value.get());
  if (!result->GetString("user", username) ||
      !result->GetString("pass", password) ||
      !result->GetString("captcha", captcha) ||
      !result->GetString("access_code", access_code)) {
      return false;
  }
  return true;
}

bool GetPassphrase(const std::string& json, std::string* passphrase,
                   std::string* mode) {
  scoped_ptr<Value> parsed_value(base::JSONReader::Read(json, false));
  if (!parsed_value.get() || !parsed_value->IsType(Value::TYPE_DICTIONARY))
    return false;

  DictionaryValue* result = static_cast<DictionaryValue*>(parsed_value.get());
  return result->GetString("passphrase", passphrase) &&
         result->GetString("mode", mode);
}

static bool GetConfiguration(const std::string& json,
                             SyncConfiguration* config) {
  scoped_ptr<Value> parsed_value(base::JSONReader::Read(json, false));
  if (!parsed_value.get() || !parsed_value->IsType(Value::TYPE_DICTIONARY))
    return false;

  DictionaryValue* result = static_cast<DictionaryValue*>(parsed_value.get());
  if (!result->GetBoolean("keepEverythingSynced", &config->sync_everything))
    return false;

  // These values need to be kept in sync with where they are written in
  // choose_datatypes.html.
  bool sync_bookmarks;
  if (!result->GetBoolean("syncBookmarks", &sync_bookmarks))
    return false;
  if (sync_bookmarks)
    config->data_types.insert(syncable::BOOKMARKS);

  bool sync_preferences;
  if (!result->GetBoolean("syncPreferences", &sync_preferences))
    return false;
  if (sync_preferences)
    config->data_types.insert(syncable::PREFERENCES);

  bool sync_themes;
  if (!result->GetBoolean("syncThemes", &sync_themes))
    return false;
  if (sync_themes)
    config->data_types.insert(syncable::THEMES);

  bool sync_passwords;
  if (!result->GetBoolean("syncPasswords", &sync_passwords))
    return false;
  if (sync_passwords)
    config->data_types.insert(syncable::PASSWORDS);

  bool sync_autofill;
  if (!result->GetBoolean("syncAutofill", &sync_autofill))
    return false;
  if (sync_autofill)
    config->data_types.insert(syncable::AUTOFILL);

  bool sync_extensions;
  if (!result->GetBoolean("syncExtensions", &sync_extensions))
    return false;
  if (sync_extensions)
    config->data_types.insert(syncable::EXTENSIONS);

  bool sync_typed_urls;
  if (!result->GetBoolean("syncTypedUrls", &sync_typed_urls))
    return false;
  if (sync_typed_urls)
    config->data_types.insert(syncable::TYPED_URLS);

  bool sync_sessions;
  if (!result->GetBoolean("syncSessions", &sync_sessions))
    return false;
  if (sync_sessions)
    config->data_types.insert(syncable::SESSIONS);

  bool sync_apps;
  if (!result->GetBoolean("syncApps", &sync_apps))
    return false;
  if (sync_apps)
    config->data_types.insert(syncable::APPS);

  // Encyption settings.
  if (!result->GetBoolean("usePassphrase", &config->use_secondary_passphrase))
    return false;

  return true;
}

void FlowHandler::HandleSubmitAuth(const ListValue* args) {
  std::string json(dom_ui_util::GetJsonResponseFromFirstArgumentInList(args));
  std::string username, password, captcha, access_code;
  if (json.empty())
    return;

  if (!GetAuthData(json, &username, &password, &captcha, &access_code)) {
    // The page sent us something that we didn't understand.
    // This probably indicates a programming error.
    NOTREACHED();
    return;
  }

  if (flow_)
    flow_->OnUserSubmittedAuth(username, password, captcha, access_code);
}

void FlowHandler::HandleConfigure(const ListValue* args) {
  std::string json(dom_ui_util::GetJsonResponseFromFirstArgumentInList(args));
  SyncConfiguration configuration;

  if (json.empty())
    return;

  if (!GetConfiguration(json, &configuration)) {
    // The page sent us something that we didn't understand.
    // This probably indicates a programming error.
    NOTREACHED();
    return;
  }

  DCHECK(flow_);
  flow_->OnUserConfigured(configuration);

  return;
}

void FlowHandler::HandlePassphraseEntry(const ListValue* args) {
  std::string json(dom_ui_util::GetJsonResponseFromFirstArgumentInList(args));

  if (json.empty())
    return;

  std::string passphrase;
  std::string mode;
  if (!GetPassphrase(json, &passphrase, &mode)) {
    // Couldn't understand what the page sent.  Indicates a programming error.
    NOTREACHED();
    return;
  }

  DCHECK(flow_);
  flow_->OnPassphraseEntry(passphrase, mode);
}

// Called by SyncSetupFlow::Advance.
void FlowHandler::ShowGaiaLogin(const DictionaryValue& args) {
  // Whenever you start a wizard, you pass in an arg so it starts on the right
  // iframe (see setup_flow.html's showTheRightIframe() method).  But when you
  // transition from one flow to another, you have to explicitly call the JS
  // function to show the next iframe.
  // So if you ever made a wizard that involved a gaia login as not the first
  // frame, this call would be necessary to ensure that this method actually
  // shows the gaia login.
  if (dom_ui_)
    dom_ui_->CallJavascriptFunction(L"showGaiaLoginIframe");

  std::string json;
  base::JSONWriter::Write(&args, false, &json);
  std::wstring javascript = std::wstring(L"showGaiaLogin") +
      L"(" + UTF8ToWide(json) + L");";
  ExecuteJavascriptInIFrame(kLoginIFrameXPath, javascript);
}

void FlowHandler::ShowGaiaSuccessAndClose() {
  ExecuteJavascriptInIFrame(kLoginIFrameXPath, L"showGaiaSuccessAndClose();");
}

void FlowHandler::ShowGaiaSuccessAndSettingUp() {
  ExecuteJavascriptInIFrame(kLoginIFrameXPath,
                            L"showGaiaSuccessAndSettingUp();");
}

// Called by SyncSetupFlow::Advance.
void FlowHandler::ShowConfigure(const DictionaryValue& args) {
  // If you're starting the wizard at the configure screen (i.e. from
  // "Customize Sync"), this will be redundant.  However, if you're coming from
  // another wizard state, this will make sure Choose Data Types is on top.
  if (dom_ui_)
    dom_ui_->CallJavascriptFunction(L"showConfigure");

  std::string json;
  base::JSONWriter::Write(&args, false, &json);
  std::wstring javascript = std::wstring(L"initializeConfigureDialog") +
      L"(" + UTF8ToWide(json) + L");";
  ExecuteJavascriptInIFrame(kChooseDataTypesIFrameXPath, javascript);
}

void FlowHandler::ShowPassphraseEntry(const DictionaryValue& args) {
  if (dom_ui_)
    dom_ui_->CallJavascriptFunction(L"showPassphrase");

  std::string json;
  base::JSONWriter::Write(&args, false, &json);
  std::wstring script = std::wstring(L"setupPassphraseDialog") +
      L"(" + UTF8ToWide(json) + L");";
  ExecuteJavascriptInIFrame(kPassphraseIFrameXPath, script);
}

void FlowHandler::ShowSettingUp() {
  if (dom_ui_)
    dom_ui_->CallJavascriptFunction(L"showSettingUp");
}

void FlowHandler::ShowSetupDone(const std::wstring& user) {
  StringValue synced_to_string(l10n_util::GetStringFUTF8(
      IDS_SYNC_NTP_SYNCED_TO, WideToUTF16Hack(user)));
  std::string json;
  base::JSONWriter::Write(&synced_to_string, false, &json);
  std::wstring javascript = std::wstring(L"setSyncedToUser") +
      L"(" + UTF8ToWide(json) + L");";
  ExecuteJavascriptInIFrame(kDoneIframeXPath, javascript);

  if (dom_ui_)
    dom_ui_->CallJavascriptFunction(L"showSetupDone", synced_to_string);

  ExecuteJavascriptInIFrame(kDoneIframeXPath,
                            L"onPageShown();");
}

void FlowHandler::ShowFirstTimeDone(const std::wstring& user) {
  ExecuteJavascriptInIFrame(kDoneIframeXPath,
                            L"setShowFirstTimeSetupSummary();");
  ShowSetupDone(user);
}

void FlowHandler::ExecuteJavascriptInIFrame(const std::wstring& iframe_xpath,
                                            const std::wstring& js) {
  if (dom_ui_) {
    RenderViewHost* rvh = dom_ui_->tab_contents()->render_view_host();
    rvh->ExecuteJavascriptInWebFrame(iframe_xpath, js);
  }
}

// Use static Run method to get an instance.
SyncSetupFlow::SyncSetupFlow(SyncSetupWizard::State start_state,
                             SyncSetupWizard::State end_state,
                             const std::string& args,
                             SyncSetupFlowContainer* container,
                             ProfileSyncService* service)
    : container_(container),
      dialog_start_args_(args),
      current_state_(start_state),
      end_state_(end_state),
      login_start_time_(base::TimeTicks::Now()),
      flow_handler_(new FlowHandler()),
      owns_flow_handler_(true),
      configuration_pending_(false),
      service_(service),
      html_dialog_window_(NULL) {
  flow_handler_->set_flow(this);
}

SyncSetupFlow::~SyncSetupFlow() {
  flow_handler_->set_flow(NULL);
  if (owns_flow_handler_) {
    delete flow_handler_;
  }
}

void SyncSetupFlow::GetDialogSize(gfx::Size* size) const {
  PrefService* prefs = service_->profile()->GetPrefs();
  gfx::Font approximate_web_font = gfx::Font(
      UTF8ToWide(prefs->GetString(prefs::kWebKitSansSerifFontFamily)),
      prefs->GetInteger(prefs::kWebKitDefaultFontSize));

  *size = gfx::GetLocalizedContentsSizeForFont(
      IDS_SYNC_SETUP_WIZARD_WIDTH_CHARS,
      IDS_SYNC_SETUP_WIZARD_HEIGHT_LINES,
      approximate_web_font);

#if defined(OS_MACOSX)
  // NOTE(akalin): This is a hack to work around a problem with font height on
  // windows.  Basically font metrics are incorrectly returned in logical units
  // instead of pixels on Windows.  Logical units are very commonly 96 DPI
  // so our localized char/line counts are too small by a factor of 96/72.
  // So we compensate for this on non-windows platform.
  //
  // TODO(akalin): Remove this hack once we fix the windows font problem (or at
  // least work around it in some other place).
  float scale_hack = 96.f/72.f;
  size->set_width(size->width() * scale_hack);
  size->set_height(size->height() * scale_hack);
#endif
}

// A callback to notify the delegate that the dialog closed.
void SyncSetupFlow::OnDialogClosed(const std::string& json_retval) {
  DCHECK(json_retval.empty());
  container_->set_flow(NULL);  // Sever ties from the wizard.
  if (current_state_ == SyncSetupWizard::DONE ||
      current_state_ == SyncSetupWizard::DONE_FIRST_TIME) {
    service_->SetSyncSetupCompleted();
  }

  // Record the state at which the user cancelled the signon dialog.
  switch (current_state_) {
    case SyncSetupWizard::GAIA_LOGIN:
      ProfileSyncService::SyncEvent(
          ProfileSyncService::CANCEL_FROM_SIGNON_WITHOUT_AUTH);
      break;
    case SyncSetupWizard::GAIA_SUCCESS:
      ProfileSyncService::SyncEvent(
          ProfileSyncService::CANCEL_DURING_SIGNON);
      break;
    case SyncSetupWizard::CONFIGURE:
    case SyncSetupWizard::ENTER_PASSPHRASE:
    case SyncSetupWizard::SETTING_UP:
      ProfileSyncService::SyncEvent(
          ProfileSyncService::CANCEL_DURING_CONFIGURE);
      break;
    case SyncSetupWizard::DONE_FIRST_TIME:
    case SyncSetupWizard::DONE:
      // TODO(sync): rename this histogram; it's tracking authorization AND
      // initial sync download time.
      UMA_HISTOGRAM_MEDIUM_TIMES("Sync.UserPerceivedAuthorizationTime",
                                 base::TimeTicks::Now() - login_start_time_);
      break;
    default:
      break;
  }

  service_->OnUserCancelledDialog();
  delete this;
}

// static
void SyncSetupFlow::GetArgsForGaiaLogin(const ProfileSyncService* service,
                                        DictionaryValue* args) {
  args->SetString("iframeToShow", "login");
  const GoogleServiceAuthError& error = service->GetAuthError();
  if (!service->last_attempted_user_email().empty()) {
    args->SetString("user", service->last_attempted_user_email());
    args->SetInteger("error", error.state());
    args->SetBoolean("editable_user", true);
  } else {
    string16 user;
    if (!service->cros_user().empty())
      user = UTF8ToUTF16(service->cros_user());
    else
      user = service->GetAuthenticatedUsername();
    args->SetString("user", user);
    args->SetInteger("error", 0);
    args->SetBoolean("editable_user", user.empty());
  }

  args->SetString("captchaUrl", error.captcha().image_url.spec());
}

// static
void SyncSetupFlow::GetArgsForEnterPassphrase(
    const ProfileSyncService* service, DictionaryValue* args) {
  args->SetString("iframeToShow", "passphrase");
  if (service->IsUsingSecondaryPassphrase())
    args->SetString("mode", "enter");
  else
    args->SetString("mode", "gaia");
}

// static
void SyncSetupFlow::GetArgsForConfigure(ProfileSyncService* service,
                                        DictionaryValue* args) {
  args->SetString("iframeToShow", "configure");

  // By default start on the data types tab.
  args->SetString("initialTab", "data-type");

  args->SetBoolean("keepEverythingSynced",
      service->profile()->GetPrefs()->GetBoolean(prefs::kKeepEverythingSynced));

  // Bookmarks, Preferences, and Themes are launched for good, there's no
  // going back now.  Check if the other data types are registered though.
  syncable::ModelTypeSet registered_types;
  service->GetRegisteredDataTypes(&registered_types);
  args->SetBoolean("passwordsRegistered",
      registered_types.count(syncable::PASSWORDS) > 0);
  args->SetBoolean("autofillRegistered",
      registered_types.count(syncable::AUTOFILL) > 0);
  args->SetBoolean("extensionsRegistered",
      registered_types.count(syncable::EXTENSIONS) > 0);
  args->SetBoolean("typedUrlsRegistered",
      registered_types.count(syncable::TYPED_URLS) > 0);
  args->SetBoolean("appsRegistered",
      registered_types.count(syncable::APPS) > 0);
  args->SetBoolean("sessionsRegistered",
      registered_types.count(syncable::SESSIONS) > 0);
  args->SetBoolean("syncBookmarks",
      service->profile()->GetPrefs()->GetBoolean(prefs::kSyncBookmarks));
  args->SetBoolean("syncPreferences",
      service->profile()->GetPrefs()->GetBoolean(prefs::kSyncPreferences));
  args->SetBoolean("syncThemes",
      service->profile()->GetPrefs()->GetBoolean(prefs::kSyncThemes));
  args->SetBoolean("syncPasswords",
      service->profile()->GetPrefs()->GetBoolean(prefs::kSyncPasswords));
  args->SetBoolean("syncAutofill",
      service->profile()->GetPrefs()->GetBoolean(prefs::kSyncAutofill));
  args->SetBoolean("syncExtensions",
      service->profile()->GetPrefs()->GetBoolean(prefs::kSyncExtensions));
  args->SetBoolean("syncSessions",
      service->profile()->GetPrefs()->GetBoolean(prefs::kSyncSessions));
  args->SetBoolean("syncTypedUrls",
      service->profile()->GetPrefs()->GetBoolean(prefs::kSyncTypedUrls));
  args->SetBoolean("syncApps",
      service->profile()->GetPrefs()->GetBoolean(prefs::kSyncApps));

  // Load the parameters for the encryption tab.
  args->SetBoolean("usePassphrase", service->IsUsingSecondaryPassphrase());
}

void SyncSetupFlow::GetDOMMessageHandlers(
    std::vector<DOMMessageHandler*>* handlers) const {
  handlers->push_back(flow_handler_);
  // We don't own flow_handler_ anymore, but it sticks around until at least
  // right after OnDialogClosed() is called (and this object is destroyed).
  owns_flow_handler_ = false;
}

// Returns true if the flow should advance to |state| based on |current_state_|.
bool SyncSetupFlow::ShouldAdvance(SyncSetupWizard::State state) {
  switch (state) {
    case SyncSetupWizard::GAIA_LOGIN:
      return current_state_ == SyncSetupWizard::FATAL_ERROR ||
             current_state_ == SyncSetupWizard::GAIA_LOGIN;
    case SyncSetupWizard::GAIA_SUCCESS:
      return current_state_ == SyncSetupWizard::GAIA_LOGIN;
    case SyncSetupWizard::CONFIGURE:
      return current_state_ == SyncSetupWizard::GAIA_SUCCESS;
    case SyncSetupWizard::CREATE_PASSPHRASE:
      return current_state_ == SyncSetupWizard::CONFIGURE;
    case SyncSetupWizard::ENTER_PASSPHRASE:
      return current_state_ == SyncSetupWizard::CONFIGURE ||
             current_state_ == SyncSetupWizard::SETTING_UP;
    case SyncSetupWizard::SETUP_ABORTED_BY_PENDING_CLEAR:
      return current_state_ == SyncSetupWizard::CONFIGURE;
    case SyncSetupWizard::SETTING_UP:
      return current_state_ == SyncSetupWizard::CONFIGURE ||
             current_state_ == SyncSetupWizard::CREATE_PASSPHRASE ||
             current_state_ == SyncSetupWizard::ENTER_PASSPHRASE;
    case SyncSetupWizard::FATAL_ERROR:
      return true;  // You can always hit the panic button.
    case SyncSetupWizard::DONE_FIRST_TIME:
    case SyncSetupWizard::DONE:
      return current_state_ == SyncSetupWizard::SETTING_UP ||
             current_state_ == SyncSetupWizard::ENTER_PASSPHRASE;
    default:
      NOTREACHED() << "Unhandled State: " << state;
      return false;
  }
}

void SyncSetupFlow::Advance(SyncSetupWizard::State advance_state) {
  if (!ShouldAdvance(advance_state)) {
    LOG(WARNING) << "Invalid state change from "
                 << current_state_ << " to " << advance_state;
    return;
  }

  switch (advance_state) {
    case SyncSetupWizard::GAIA_LOGIN: {
      DictionaryValue args;
      SyncSetupFlow::GetArgsForGaiaLogin(service_, &args);
      flow_handler_->ShowGaiaLogin(args);
      break;
    }
    case SyncSetupWizard::GAIA_SUCCESS:
      if (end_state_ == SyncSetupWizard::GAIA_SUCCESS) {
        flow_handler_->ShowGaiaSuccessAndClose();
        break;
      }
      advance_state = SyncSetupWizard::CONFIGURE;
      //  Fall through.
    case SyncSetupWizard::CONFIGURE: {
      DictionaryValue args;
      SyncSetupFlow::GetArgsForConfigure(service_, &args);
      args.SetString("initialTab", "data-type");
      flow_handler_->ShowConfigure(args);
      break;
    }
    case SyncSetupWizard::CREATE_PASSPHRASE: {
      DictionaryValue args;
      args.SetString("mode", "new");
      flow_handler_->ShowPassphraseEntry(args);
      break;
    }
    case SyncSetupWizard::ENTER_PASSPHRASE: {
      DictionaryValue args;
      SyncSetupFlow::GetArgsForEnterPassphrase(service_, &args);
      flow_handler_->ShowPassphraseEntry(args);
      break;
    }
    case SyncSetupWizard::SETUP_ABORTED_BY_PENDING_CLEAR: {
      DictionaryValue args;
      SyncSetupFlow::GetArgsForConfigure(service_, &args);
      args.SetBoolean("was_aborted", true);
      flow_handler_->ShowConfigure(args);
      break;
    }
    case SyncSetupWizard::SETTING_UP: {
      flow_handler_->ShowSettingUp();
      break;
    }
    case SyncSetupWizard::FATAL_ERROR: {
      // This shows the user the "Could not connect to server" error.
      // TODO(sync): Update this error messaging.
      DictionaryValue args;
      SyncSetupFlow::GetArgsForGaiaLogin(service_, &args);
      args.SetInteger("error", GoogleServiceAuthError::CONNECTION_FAILED);
      flow_handler_->ShowGaiaLogin(args);
      break;
    }
    case SyncSetupWizard::DONE_FIRST_TIME:
      flow_handler_->ShowFirstTimeDone(
          UTF16ToWide(service_->GetAuthenticatedUsername()));
      break;
    case SyncSetupWizard::DONE:
      flow_handler_->ShowSetupDone(
          UTF16ToWide(service_->GetAuthenticatedUsername()));
      break;
    default:
      NOTREACHED() << "Invalid advance state: " << advance_state;
  }
  current_state_ = advance_state;
}

void SyncSetupFlow::Focus() {
#if defined(OS_MACOSX)
  if (html_dialog_window_) {
    platform_util::ActivateWindow(html_dialog_window_);
  }
#else
  // TODO(csilv): We don't currently have a way to get the reference to the
  // dialog on windows/linux.  This can be resolved by a cross platform
  // implementation of HTML dialogs as described by akalin below.
  NOTIMPLEMENTED();
#endif  // defined(OS_MACOSX)
}

// static
SyncSetupFlow* SyncSetupFlow::Run(ProfileSyncService* service,
                                  SyncSetupFlowContainer* container,
                                  SyncSetupWizard::State start,
                                  SyncSetupWizard::State end,
                                  gfx::NativeWindow parent_window) {
  DictionaryValue args;
  if (start == SyncSetupWizard::GAIA_LOGIN)
    SyncSetupFlow::GetArgsForGaiaLogin(service, &args);
  else if (start == SyncSetupWizard::CONFIGURE)
    SyncSetupFlow::GetArgsForConfigure(service, &args);
  else if (start == SyncSetupWizard::ENTER_PASSPHRASE)
    SyncSetupFlow::GetArgsForEnterPassphrase(service, &args);
  std::string json_args;
  base::JSONWriter::Write(&args, false, &json_args);

  SyncSetupFlow* flow = new SyncSetupFlow(start, end, json_args,
      container, service);
#if defined(OS_MACOSX)
  // TODO(akalin): Figure out a cleaner way to do this than to have this
  // gross per-OS behavior, i.e. have a cross-platform ShowHtmlDialog()
  // function that is not tied to a browser instance.  Note that if we do
  // that, we'll have to fix sync_setup_wizard_unittest.cc as it relies on
  // being able to intercept ShowHtmlDialog() calls.
  flow->html_dialog_window_ =
      html_dialog_window_controller::ShowHtmlDialog(
          flow, service->profile());
#else
  Browser* b = BrowserList::GetLastActive();
  if (b) {
    b->BrowserShowHtmlDialog(flow, parent_window);
  } else {
    delete flow;
    return NULL;
  }
#endif  // defined(OS_MACOSX)
  return flow;
}

void SyncSetupFlow::OnUserSubmittedAuth(const std::string& username,
                                        const std::string& password,
                                        const std::string& captcha,
                                        const std::string& access_code) {
  service_->OnUserSubmittedAuth(username, password, captcha, access_code);
}

void SyncSetupFlow::OnUserConfigured(const SyncConfiguration& configuration) {
  // Store the configuration in case we need more information.
  configuration_ = configuration;
  configuration_pending_ = true;

  // If the user is activating secondary passphrase for the first time,
  // we need to prompt them to enter one.
  if (configuration.use_secondary_passphrase &&
      !service_->IsUsingSecondaryPassphrase()) {
    // TODO(tim): If we could download the Nigori node first before any other
    // types, we could do that prior to showing the configure page so that we
    // could pre-populate the 'Use an encryption passphrase' checkbox.
    // http://crbug.com/60182
    Advance(SyncSetupWizard::CREATE_PASSPHRASE);
    return;
  }

  OnConfigurationComplete();
}

void SyncSetupFlow::OnConfigurationComplete() {
  if (!configuration_pending_)
    return;

  // Go to the "loading..." screen."
  Advance(SyncSetupWizard::SETTING_UP);

  // If we are activating the passphrase, we need to have one supplied.
  DCHECK(service_->IsUsingSecondaryPassphrase() ||
         !configuration_.use_secondary_passphrase ||
         configuration_.secondary_passphrase.length() > 0);

  if (configuration_.use_secondary_passphrase &&
      !service_->IsUsingSecondaryPassphrase()) {
    service_->SetPassphrase(configuration_.secondary_passphrase, true);
  }

  service_->OnUserChoseDatatypes(configuration_.sync_everything,
                                 configuration_.data_types);

  configuration_pending_ = false;
}

void SyncSetupFlow::OnPassphraseEntry(const std::string& passphrase,
                                      const std::string& mode) {
  if (current_state_ == SyncSetupWizard::ENTER_PASSPHRASE) {
    service_->SetPassphrase(passphrase, mode == std::string("enter"));
    Advance(SyncSetupWizard::SETTING_UP);
  } else if (configuration_pending_) {
    DCHECK_EQ(SyncSetupWizard::CREATE_PASSPHRASE, current_state_);
    configuration_.secondary_passphrase = passphrase;
    OnConfigurationComplete();
  }
}
