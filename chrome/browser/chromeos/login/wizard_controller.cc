// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/wizard_controller.h"

#include <gdk/gdk.h>
#include <signal.h>
#include <sys/types.h>

#include <string>
#include <vector>

#include "app/resource_bundle.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/cros/input_method_library.h"
#include "chrome/browser/chromeos/cros/login_library.h"
#include "chrome/browser/chromeos/cros/system_library.h"
#include "chrome/browser/chromeos/customization_document.h"
#include "chrome/browser/chromeos/input_method/input_method_util.h"
#include "chrome/browser/chromeos/language_preferences.h"
#include "chrome/browser/chromeos/login/account_screen.h"
#include "chrome/browser/chromeos/login/apply_services_customization.h"
#include "chrome/browser/chromeos/login/background_view.h"
#include "chrome/browser/chromeos/login/eula_view.h"
#include "chrome/browser/chromeos/login/existing_user_controller.h"
#include "chrome/browser/chromeos/login/helper.h"
#include "chrome/browser/chromeos/login/html_page_screen.h"
#include "chrome/browser/chromeos/login/language_switch_menu.h"
#include "chrome/browser/chromeos/login/login_screen.h"
#include "chrome/browser/chromeos/login/login_utils.h"
#include "chrome/browser/chromeos/login/network_screen.h"
#include "chrome/browser/chromeos/login/registration_screen.h"
#include "chrome/browser/chromeos/login/rounded_rect_painter.h"
#include "chrome/browser/chromeos/login/update_screen.h"
#include "chrome/browser/chromeos/login/user_image_screen.h"
#include "chrome/browser/chromeos/login/user_manager.h"
#include "chrome/browser/chromeos/login/wizard_accessibility_helper.h"
#include "chrome/browser/chromeos/wm_ipc.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profile_manager.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/pref_names.h"
#include "cros/chromeos_wm_ipc_enums.h"
#include "unicode/timezone.h"
#include "views/accelerator.h"
#include "views/painter.h"
#include "views/view.h"
#include "views/widget/widget_gtk.h"

namespace {

// A boolean pref of the OOBE complete flag.
const char kOobeComplete[] = "OobeComplete";

// Path to OEM partner startup customization manifest.
const char kStartupCustomizationManifestPath[] =
    "/mnt/partner_partition/etc/chromeos/startup_manifest.json";

// Path to flag file indicating that OOBE was completed successfully.
const char kOobeCompleteFlagFilePath[] =
    "/home/chronos/.oobe_completed";

// Upadate window will be behind the curtain at most |kMaximalCurtainTimeSec|.
const int kMaximalCurtainTimeSec = 15;

// Time in seconds that we wait for the device to reboot.
// If reboot didn't happen, ask user to reboot device manually.
const int kWaitForRebootTimeSec = 3;

// RootView of the Widget WizardController creates. Contains the contents of the
// WizardController.
class ContentView : public views::View {
 public:
  ContentView()
      : accel_enable_accessibility_(
            WizardAccessibilityHelper::GetAccelerator()) {
    AddAccelerator(accel_enable_accessibility_);
#if !defined(OFFICIAL_BUILD)
    accel_account_screen_ = views::Accelerator(app::VKEY_A,
                                               false, true, true);
    accel_login_screen_ = views::Accelerator(app::VKEY_L,
                                             false, true, true);
    accel_network_screen_ = views::Accelerator(app::VKEY_N,
                                               false, true, true);
    accel_update_screen_ = views::Accelerator(app::VKEY_U,
                                              false, true, true);
    accel_image_screen_ = views::Accelerator(app::VKEY_I,
                                             false, true, true);
    accel_eula_screen_ = views::Accelerator(app::VKEY_E,
                                            false, true, true);
    accel_register_screen_ = views::Accelerator(app::VKEY_R,
                                                false, true, true);
    AddAccelerator(accel_account_screen_);
    AddAccelerator(accel_login_screen_);
    AddAccelerator(accel_network_screen_);
    AddAccelerator(accel_update_screen_);
    AddAccelerator(accel_image_screen_);
    AddAccelerator(accel_eula_screen_);
    AddAccelerator(accel_register_screen_);
#endif
  }

  ~ContentView() {
    NotificationService::current()->Notify(
        NotificationType::WIZARD_CONTENT_VIEW_DESTROYED,
        NotificationService::AllSources(),
        NotificationService::NoDetails());
  }

  bool AcceleratorPressed(const views::Accelerator& accel) {
    WizardController* controller = WizardController::default_controller();
    if (!controller)
      return false;

    if (accel == accel_enable_accessibility_) {
      WizardAccessibilityHelper::GetInstance()->EnableAccessibility(
          controller->contents());
#if !defined(OFFICIAL_BUILD)
    } else if (accel == accel_account_screen_) {
      controller->ShowAccountScreen();
    } else if (accel == accel_login_screen_) {
      controller->ShowLoginScreen();
    } else if (accel == accel_network_screen_) {
      controller->ShowNetworkScreen();
    } else if (accel == accel_update_screen_) {
      controller->ShowUpdateScreen();
    } else if (accel == accel_image_screen_) {
      controller->ShowUserImageScreen();
    } else if (accel == accel_eula_screen_) {
      controller->ShowEulaScreen();
    } else if (accel == accel_register_screen_) {
      controller->ShowRegistrationScreen();
#endif
    } else {
      return false;
    }

    return true;
  }

  virtual void Layout() {
    for (int i = 0; i < GetChildViewCount(); ++i) {
      views::View* cur = GetChildViewAt(i);
      if (cur->IsVisible())
        cur->SetBounds(0, 0, width(), height());
    }
  }

 private:
  scoped_ptr<views::Painter> painter_;

#if !defined(OFFICIAL_BUILD)
  views::Accelerator accel_account_screen_;
  views::Accelerator accel_login_screen_;
  views::Accelerator accel_network_screen_;
  views::Accelerator accel_update_screen_;
  views::Accelerator accel_image_screen_;
  views::Accelerator accel_eula_screen_;
  views::Accelerator accel_register_screen_;
#endif
  views::Accelerator accel_enable_accessibility_;

  DISALLOW_COPY_AND_ASSIGN(ContentView);
};

void DeleteWizardControllerAndLaunchBrowser(WizardController* controller) {
  delete controller;
  // Launch browser after controller is deleted and its windows are closed.
  chromeos::LoginUtils::Get()->EnableBrowserLaunch(true);
  ChromeThread::PostTask(
      ChromeThread::UI,
      FROM_HERE,
      NewRunnableFunction(&chromeos::LoginUtils::DoBrowserLaunch,
                          ProfileManager::GetDefaultProfile()));
}

const chromeos::StartupCustomizationDocument* LoadStartupManifest() {
  // Load partner customization startup manifest if it is available.
  FilePath startup_manifest_path(kStartupCustomizationManifestPath);
  if (file_util::PathExists(startup_manifest_path)) {
    scoped_ptr<chromeos::StartupCustomizationDocument> customization(
        new chromeos::StartupCustomizationDocument());
    bool manifest_loaded = customization->LoadManifestFromFile(
        startup_manifest_path);
    if (manifest_loaded) {
      LOG(INFO) << "Startup manifest loaded successfully";
      return customization.release();
    } else {
      LOG(ERROR) << "Error loading startup manifest. " <<
          kStartupCustomizationManifestPath;
    }
  }

  return NULL;
}

}  // namespace

const char WizardController::kNetworkScreenName[] = "network";
const char WizardController::kLoginScreenName[] = "login";
const char WizardController::kAccountScreenName[] = "account";
const char WizardController::kUpdateScreenName[] = "update";
const char WizardController::kUserImageScreenName[] = "image";
const char WizardController::kEulaScreenName[] = "eula";
const char WizardController::kRegistrationScreenName[] = "register";
const char WizardController::kHTMLPageScreenName[] = "html";

// Passing this parameter as a "first screen" initiates full OOBE flow.
const char WizardController::kOutOfBoxScreenName[] = "oobe";

// Special test value that commands not to create any window yet.
const char WizardController::kTestNoScreenName[] = "test:nowindow";

// Initialize default controller.
// static
WizardController* WizardController::default_controller_ = NULL;

///////////////////////////////////////////////////////////////////////////////
// WizardController, public:

WizardController::WizardController()
    : widget_(NULL),
      background_widget_(NULL),
      background_view_(NULL),
      contents_(NULL),
      current_screen_(NULL),
#if defined(OFFICIAL_BUILD)
      is_official_build_(true),
#else
      is_official_build_(false),
#endif
      is_out_of_box_(false),
      is_test_mode_(false),
      observer_(NULL) {
  DCHECK(default_controller_ == NULL);
  default_controller_ = this;
}

WizardController::~WizardController() {
  // Close ends up deleting the widget.
  if (background_widget_)
    background_widget_->Close();

  if (widget_)
    widget_->Close();

  default_controller_ = NULL;
}

void WizardController::Init(const std::string& first_screen_name,
                            const gfx::Rect& screen_bounds) {
  LOG(INFO) << "Starting OOBE wizard with screen: " << first_screen_name;
  DCHECK(!contents_);
  first_screen_name_ = first_screen_name;

  // When device is not registered yet we need to load startup manifest as well.
  // In case of OOBE (network-EULA-update) manifest has been loaded in
  // ShowLoginWizard().
  if (IsOobeCompleted() && !IsDeviceRegistered())
    SetCustomization(LoadStartupManifest());

  screen_bounds_ = screen_bounds;
  contents_ = new ContentView();

  bool oobe_complete = IsOobeCompleted();
  if (!oobe_complete || first_screen_name == kOutOfBoxScreenName) {
    is_out_of_box_ = true;
  }

  ShowFirstScreen(first_screen_name);
}

void WizardController::Show() {
  // In tests we might startup without initial screen
  // so widget_ hasn't been created yet.
  if (first_screen_name_ != kTestNoScreenName)
    DCHECK(widget_);
  if (widget_)
    widget_->Show();
}

void WizardController::ShowBackground(const gfx::Rect& bounds) {
  DCHECK(!background_widget_);
  background_widget_ =
      chromeos::BackgroundView::CreateWindowContainingView(bounds,
                                                           GURL(),
                                                           &background_view_);
  background_view_->SetOobeProgressBarVisible(true);
  background_widget_->Show();
}

void WizardController::OwnBackground(
    views::Widget* background_widget,
    chromeos::BackgroundView* background_view) {
  DCHECK(!background_widget_);
  background_widget_ = background_widget;
  background_view_ = background_view;
  background_view_->OnOwnerChanged();
}

chromeos::NetworkScreen* WizardController::GetNetworkScreen() {
  if (!network_screen_.get())
    network_screen_.reset(new chromeos::NetworkScreen(this));
  return network_screen_.get();
}

chromeos::LoginScreen* WizardController::GetLoginScreen() {
  if (!login_screen_.get())
    login_screen_.reset(new chromeos::LoginScreen(this));
  return login_screen_.get();
}

chromeos::AccountScreen* WizardController::GetAccountScreen() {
  if (!account_screen_.get())
    account_screen_.reset(new chromeos::AccountScreen(this));
  return account_screen_.get();
}

chromeos::UpdateScreen* WizardController::GetUpdateScreen() {
  if (!update_screen_.get()) {
    update_screen_.reset(new chromeos::UpdateScreen(this));
    update_screen_->SetMaximalCurtainTime(kMaximalCurtainTimeSec);
    update_screen_->SetRebootCheckDelay(kWaitForRebootTimeSec);
  }
  return update_screen_.get();
}

chromeos::UserImageScreen* WizardController::GetUserImageScreen() {
  if (!user_image_screen_.get())
    user_image_screen_.reset(new chromeos::UserImageScreen(this));
  return user_image_screen_.get();
}

chromeos::EulaScreen* WizardController::GetEulaScreen() {
  if (!eula_screen_.get())
    eula_screen_.reset(new chromeos::EulaScreen(this));
  return eula_screen_.get();
}

chromeos::RegistrationScreen* WizardController::GetRegistrationScreen() {
  if (!registration_screen_.get())
    registration_screen_.reset(new chromeos::RegistrationScreen(this));
  return registration_screen_.get();
}

chromeos::HTMLPageScreen* WizardController::GetHTMLPageScreen() {
  if (!html_page_screen_.get()) {
    CommandLine* command_line = CommandLine::ForCurrentProcess();
    std::string url;
    // It's strange but args may contains empty strings.
    for (size_t i = 0; i < command_line->args().size(); i++) {
      if (!command_line->args()[i].empty()) {
        DCHECK(url.empty()) << "More than one URL in command line";
        url = command_line->args()[i];
      }
    }
    DCHECK(!url.empty()) << "No URL in commane line";
    html_page_screen_.reset(new chromeos::HTMLPageScreen(this, url));
  }
  return html_page_screen_.get();
}

void WizardController::ShowNetworkScreen() {
  SetStatusAreaVisible(false);
  SetCurrentScreen(GetNetworkScreen());
  background_view_->SetOobeProgress(chromeos::BackgroundView::SELECT_NETWORK);
}

chromeos::ExistingUserController* WizardController::ShowLoginScreen() {
  SetStatusAreaVisible(true);
  background_view_->SetOobeProgress(chromeos::BackgroundView::SIGNIN);

  // Initiate services customization.
  chromeos::ApplyServicesCustomization::StartIfNeeded();

  // When run under automation test show plain login screen.
  if (!is_test_mode_ &&
      chromeos::CrosLibrary::Get()->EnsureLoaded() &&
      CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableLoginImages)) {
    std::vector<chromeos::UserManager::User> users =
        chromeos::UserManager::Get()->GetUsers();
    // ExistingUserController deletes itself.
    gfx::Rect screen_bounds;
    background_widget_->GetBounds(&screen_bounds, true);
    chromeos::ExistingUserController* controller =
        new chromeos::ExistingUserController(users, screen_bounds);
    controller->OwnBackground(background_widget_, background_view_);
    controller->Init();
    background_widget_ = NULL;
    background_view_ = NULL;

    MessageLoop::current()->DeleteSoon(FROM_HERE, this);

    return controller;
  }

  SetCurrentScreen(GetLoginScreen());
  return NULL;
}

void WizardController::ShowAccountScreen() {
  LOG(INFO) << "Showing create account screen.";
  SetStatusAreaVisible(true);
  SetCurrentScreen(GetAccountScreen());
}

void WizardController::ShowUpdateScreen() {
  LOG(INFO) << "Showing update screen.";
  SetStatusAreaVisible(true);
  SetCurrentScreen(GetUpdateScreen());
  // There is no special step for update.
#if defined(OFFICIAL_BUILD)
  background_view_->SetOobeProgress(chromeos::BackgroundView::EULA);
#else
  background_view_->SetOobeProgress(chromeos::BackgroundView::SELECT_NETWORK);
#endif
}

void WizardController::ShowUserImageScreen() {
  LOG(INFO) << "Showing user image screen.";
  SetStatusAreaVisible(false);
  SetCurrentScreen(GetUserImageScreen());
  background_view_->SetOobeProgress(chromeos::BackgroundView::PICTURE);
}

void WizardController::ShowEulaScreen() {
  LOG(INFO) << "Showing EULA screen.";
  SetStatusAreaVisible(false);
  SetCurrentScreen(GetEulaScreen());
#if defined(OFFICIAL_BUILD)
  background_view_->SetOobeProgress(chromeos::BackgroundView::EULA);
#endif
}

void WizardController::ShowRegistrationScreen() {
  if (!GetCustomization() ||
      !GURL(GetCustomization()->registration_url()).is_valid()) {
    LOG(INFO) <<
        "Skipping registration screen: manifest not defined or invalid URL.";
    OnRegistrationSkipped();
    return;
  }
  LOG(INFO) << "Showing registration screen.";
  SetStatusAreaVisible(true);
  SetCurrentScreen(GetRegistrationScreen());
#if defined(OFFICIAL_BUILD)
  background_view_->SetOobeProgress(chromeos::BackgroundView::REGISTRATION);
#endif
}

void WizardController::ShowHTMLPageScreen() {
  LOG(INFO) << "Showing HTML page screen.";
  SetStatusAreaVisible(true);
  background_view_->SetOobeProgressBarVisible(false);
  SetCurrentScreen(GetHTMLPageScreen());
}

void WizardController::SetCustomization(
    const chromeos::StartupCustomizationDocument* customization) {
  customization_.reset(customization);
}

const chromeos::StartupCustomizationDocument*
    WizardController::GetCustomization() const {
  return customization_.get();
}

void WizardController::SkipRegistration() {
  if (current_screen_ == GetRegistrationScreen())
    OnRegistrationSkipped();
  else
    LOG(ERROR) << "Registration screen is not active.";
}

// static
void WizardController::RegisterPrefs(PrefService* local_state) {
  local_state->RegisterBooleanPref(kOobeComplete, false);
  // Check if the pref is already registered in case
  // Preferences::RegisterUserPrefs runs before this code in the future.
  if (local_state->FindPreference(prefs::kAccessibilityEnabled) == NULL) {
    local_state->RegisterBooleanPref(prefs::kAccessibilityEnabled, false);
  }
}

///////////////////////////////////////////////////////////////////////////////
// WizardController, ExitHandlers:
void WizardController::OnLoginSignInSelected() {
  // Don't show user image screen in case of automated testing.
  if (is_test_mode_) {
    MessageLoop::current()->DeleteSoon(FROM_HERE, this);
    return;
  }
  // Don't launch browser until we pass image screen.
  chromeos::LoginUtils::Get()->EnableBrowserLaunch(false);
  ShowUserImageScreen();
}

void WizardController::OnLoginGuestUser() {
  // We're on the stack, so don't try and delete us now.
  MessageLoop::current()->DeleteSoon(FROM_HERE, this);
}

void WizardController::OnLoginCreateAccount() {
  ShowAccountScreen();
}

void WizardController::OnNetworkConnected() {
  if (is_official_build_) {
    ShowEulaScreen();
  } else {
    ShowUpdateScreen();
    GetUpdateScreen()->StartUpdate();
  }
}

void WizardController::OnNetworkOffline() {
  // TODO(dpolukhin): if(is_out_of_box_) we cannot work offline and
  // should report some error message here and stay on the same screen.
  ShowLoginScreen();
}

void WizardController::OnAccountCreateBack() {
  chromeos::ExistingUserController* controller = ShowLoginScreen();
  if (controller)
    controller->SelectNewUser();
}

void WizardController::OnAccountCreated() {
  chromeos::ExistingUserController* controller = ShowLoginScreen();
  if (controller)
    controller->LoginNewUser(username_, password_);
  else
    Login(username_, password_);
  // TODO(dpolukhin): clear password memory for real. Now it is not
  // a problem because we can't extract password from the form.
  password_.clear();
}

void WizardController::OnConnectionFailed() {
  // TODO(dpolukhin): show error message after login screen is displayed.
  ShowLoginScreen();
}

void WizardController::OnUpdateCompleted() {
  OnOOBECompleted();
}

void WizardController::OnEulaAccepted() {
  ShowUpdateScreen();
  GetUpdateScreen()->StartUpdate();
}

void WizardController::OnUpdateErrorCheckingForUpdate() {
  // TODO(nkostylev): Update should be required during OOBE.
  // We do not want to block users from being able to proceed to the login
  // screen if there is any error checking for an update.
  // They could use "browse without sign-in" feature to set up the network to be
  // able to perform the update later.
  OnOOBECompleted();
}

void WizardController::OnUpdateErrorUpdating() {
  // If there was an error while getting or applying the update,
  // return to network selection screen.
  // TODO(nkostylev): Show message to the user explaining update error.
  // TODO(nkostylev): Update should be required during OOBE.
  // Temporary fix, need to migrate to new API. http://crosbug.com/4321
  OnOOBECompleted();
}

void WizardController::OnUserImageSelected() {
  // We're on the stack, so don't try and delete us now.
  // We should launch browser only after we delete the controller and close
  // its windows.
  ChromeThread::PostTask(
      ChromeThread::UI,
      FROM_HERE,
      NewRunnableFunction(&DeleteWizardControllerAndLaunchBrowser,
                          this));
  // TODO(avayvod): Sync image with Google Sync.
}

void WizardController::OnUserImageSkipped() {
  OnUserImageSelected();
}

void WizardController::OnRegistrationSuccess() {
  MarkDeviceRegistered();
  if (chromeos::UserManager::Get()->logged_in_user().email().empty()) {
    chromeos::LoginUtils::Get()->CompleteOffTheRecordLogin(start_url_);
  } else {
    ShowUserImageScreen();
  }
}

void WizardController::OnRegistrationSkipped() {
  // TODO(nkostylev): Track in a histogram?
  OnRegistrationSuccess();
}

void WizardController::OnOOBECompleted() {
  MarkOobeCompleted();
  ShowLoginScreen();
}

///////////////////////////////////////////////////////////////////////////////
// WizardController, private:

views::WidgetGtk* WizardController::CreateScreenWindow(
    const gfx::Rect& bounds, bool initial_show) {
  views::WidgetGtk* window =
      new views::WidgetGtk(views::WidgetGtk::TYPE_WINDOW);
  widget_ = window;
  window->MakeTransparent();
  // Window transparency makes background flicker through controls that
  // are constantly updating its contents (like image view with video
  // stream). Hence enabling double buffer.
  window->EnableDoubleBuffer(true);
  window->Init(NULL, bounds);
  std::vector<int> params;
  // For initial show WM would animate background window.
  // Otherwise it stays unchaged.
  params.push_back(initial_show);
  chromeos::WmIpc::instance()->SetWindowType(
      window->GetNativeView(),
      chromeos::WM_IPC_WINDOW_LOGIN_GUEST,
      &params);
  window->SetContentsView(contents_);
  return window;
}

gfx::Rect WizardController::GetWizardScreenBounds(int screen_width,
                                                  int screen_height) const {
  int offset_x = (screen_bounds_.width() - screen_width) / 2;
  int offset_y = (screen_bounds_.height() - screen_height) / 2;
  int window_x = screen_bounds_.x() + offset_x;
  int window_y = screen_bounds_.y() + offset_y;
  return gfx::Rect(window_x, window_y, screen_width, screen_height);
}


void WizardController::SetCurrentScreen(WizardScreen* new_current) {
  if (current_screen_ == new_current ||
      new_current == NULL)
    return;

  bool initial_show = true;
  if (current_screen_) {
    initial_show = false;
    current_screen_->Hide();
  }

  current_screen_ = new_current;
  bool force_widget_show = false;
  views::WidgetGtk* window = NULL;

  gfx::Rect current_bounds;
  if (widget_)
    widget_->GetBounds(&current_bounds, false);
  gfx::Size new_screen_size = current_screen_->GetScreenSize();
  gfx::Rect new_bounds = GetWizardScreenBounds(new_screen_size.width(),
                                               new_screen_size.height());
  if (new_bounds != current_bounds) {
    if (widget_)
      widget_->Close();
    force_widget_show = true;
    window = CreateScreenWindow(new_bounds, initial_show);
  }
  current_screen_->Show();
  contents_->Layout();
  contents_->SchedulePaint();
  if (force_widget_show) {
    // This keeps the window from flashing at startup.
    GdkWindow* gdk_window = window->GetNativeView()->window;
    gdk_window_set_back_pixmap(gdk_window, NULL, false);
    if (widget_)
      widget_->Show();
  }
}

void WizardController::SetStatusAreaVisible(bool visible) {
  // When ExistingUserController passes background ownership
  // to WizardController it happens after screen is shown.
  if (background_view_) {
    background_view_->SetStatusAreaVisible(visible);
  }
}

void WizardController::ShowFirstScreen(const std::string& first_screen_name) {
  if (first_screen_name == kNetworkScreenName) {
    ShowNetworkScreen();
  } else if (first_screen_name == kLoginScreenName) {
    // This flag is passed if we're running under automation test.
    is_test_mode_ = true;
    ShowLoginScreen();
  } else if (first_screen_name == kAccountScreenName) {
    ShowAccountScreen();
  } else if (first_screen_name == kUpdateScreenName) {
    ShowUpdateScreen();
    GetUpdateScreen()->StartUpdate();
  } else if (first_screen_name == kUserImageScreenName) {
    ShowUserImageScreen();
  } else if (first_screen_name == kEulaScreenName) {
    ShowEulaScreen();
  } else if (first_screen_name == kRegistrationScreenName) {
    if (is_official_build_) {
      ShowRegistrationScreen();
    } else {
      // Just proceed to image screen.
      OnRegistrationSuccess();
    }
  } else if (first_screen_name == kHTMLPageScreenName) {
    ShowHTMLPageScreen();
  } else if (first_screen_name != kTestNoScreenName) {
    if (is_out_of_box_) {
      ShowNetworkScreen();
    } else {
      ShowLoginScreen();
    }
  }
}

void WizardController::Login(const std::string& username,
                             const std::string& password) {
  chromeos::LoginScreen* login = GetLoginScreen();
  if (username.empty())
    return;
  login->view()->SetUsername(username);

  if (password.empty())
    return;
  login->view()->SetPassword(password);
  login->view()->Login();
}

// static
bool WizardController::IsOobeCompleted() {
  return g_browser_process->local_state()->GetBoolean(kOobeComplete);
}

// static
void WizardController::MarkOobeCompleted() {
  PrefService* prefs = g_browser_process->local_state();
  prefs->SetBoolean(kOobeComplete, true);
  // Make sure that changes are reflected immediately.
  prefs->SavePersistentPrefs();
}

// static
bool WizardController::IsDeviceRegistered() {
  FilePath oobe_complete_flag_file_path(kOobeCompleteFlagFilePath);
  return file_util::PathExists(oobe_complete_flag_file_path);
}

// static
void WizardController::MarkDeviceRegistered() {
  // Create flag file for boot-time init scripts.
  FilePath oobe_complete_path(kOobeCompleteFlagFilePath);
  FILE* oobe_flag_file = file_util::OpenFile(oobe_complete_path, "w+b");
  DCHECK(oobe_flag_file != NULL) << kOobeCompleteFlagFilePath;
  if (oobe_flag_file != NULL)
    file_util::CloseFile(oobe_flag_file);
}

///////////////////////////////////////////////////////////////////////////////
// WizardController, chromeos::ScreenObserver overrides:
void WizardController::OnExit(ExitCodes exit_code) {
  LOG(INFO) << "Wizard screen exit code: " << exit_code;
  switch (exit_code) {
    case LOGIN_SIGN_IN_SELECTED:
      OnLoginSignInSelected();
      break;
    case LOGIN_GUEST_SELECTED:
      OnLoginGuestUser();
      break;
    case LOGIN_CREATE_ACCOUNT:
      OnLoginCreateAccount();
      break;
    case NETWORK_CONNECTED:
      OnNetworkConnected();
      break;
    case NETWORK_OFFLINE:
      OnNetworkOffline();
      break;
    case ACCOUNT_CREATE_BACK:
      OnAccountCreateBack();
      break;
    case ACCOUNT_CREATED:
      OnAccountCreated();
      break;
    case CONNECTION_FAILED:
      OnConnectionFailed();
      break;
    case UPDATE_INSTALLED:
    case UPDATE_NOUPDATE:
      OnUpdateCompleted();
      break;
    case UPDATE_ERROR_CHECKING_FOR_UPDATE:
      OnUpdateErrorCheckingForUpdate();
      break;
    case UPDATE_ERROR_UPDATING:
      OnUpdateErrorUpdating();
      break;
    case USER_IMAGE_SELECTED:
      OnUserImageSelected();
      break;
    case USER_IMAGE_SKIPPED:
      OnUserImageSkipped();
      break;
    case EULA_ACCEPTED:
      OnEulaAccepted();
      break;
    case EULA_BACK:
      ShowNetworkScreen();
      break;
    case REGISTRATION_SUCCESS:
      OnRegistrationSuccess();
      break;
    case REGISTRATION_SKIPPED:
      OnRegistrationSkipped();
      break;
    default:
      NOTREACHED();
  }
}

void WizardController::OnSetUserNamePassword(const std::string& username,
                                             const std::string& password) {
  username_ = username;
  password_ = password;
}

///////////////////////////////////////////////////////////////////////////////
// WizardController, WizardScreen overrides:
views::View* WizardController::GetWizardView() {
  return contents_;
}

chromeos::ScreenObserver* WizardController::GetObserver(WizardScreen* screen) {
  return observer_ ? observer_ : this;
}

namespace browser {

// Declared in browser_dialogs.h so that others don't need to depend on our .h.
void ShowLoginWizard(const std::string& first_screen_name,
                     const gfx::Size& size) {
  LOG(INFO) << "showing login screen: " << first_screen_name;

  // The login screen will enable alternate keyboard layouts, but we don't want
  // to start the IME process unless one is selected.
  chromeos::CrosLibrary::Get()->GetInputMethodLibrary()->
      SetDeferImeStartup(true);
  // Tell the window manager that the user isn't logged in.
  chromeos::WmIpc::instance()->SetLoggedInProperty(false);

  // Set up keyboards. For example, when |locale| is "en-US", enable US qwerty
  // and US dvorak keyboard layouts.
  if (g_browser_process && g_browser_process->local_state()) {
    const std::string locale = g_browser_process->GetApplicationLocale();
    const std::string initial_input_method_id =
        g_browser_process->local_state()->GetString(
            chromeos::language_prefs::kPreferredKeyboardLayout);
    chromeos::input_method::EnableInputMethods(
        locale, chromeos::input_method::kKeyboardLayoutsOnly,
        initial_input_method_id);
  }

  gfx::Rect screen_bounds(chromeos::CalculateScreenBounds(size));

  // Check whether we need to execute OOBE process.
  bool oobe_complete = WizardController::IsOobeCompleted();

  if (first_screen_name.empty() &&
      oobe_complete &&
      chromeos::CrosLibrary::Get()->EnsureLoaded() &&
      CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableLoginImages)) {
    std::vector<chromeos::UserManager::User> users =
        chromeos::UserManager::Get()->GetUsers();

    // Fix for users who updated device and thus never passed register screen.
    // If we already have user we assume that it is not a second part of OOBE.
    // See http://crosbug.com/6289
    if (!WizardController::IsDeviceRegistered() && !users.empty()) {
      LOG(INFO) << "Mark device registered because there are remembered users: "
                << users.size();
      WizardController::MarkDeviceRegistered();
    }

    // ExistingUserController deletes itself.
    (new chromeos::ExistingUserController(users, screen_bounds))->Init();

    // Initiate services customization.
    chromeos::ApplyServicesCustomization::StartIfNeeded();

    return;
  }

  // Create and show the wizard.
  WizardController* controller = new WizardController();

  // Load startup manifest.
  controller->SetCustomization(LoadStartupManifest());

  std::string locale;
  if (controller->GetCustomization()) {
    // Switch to initial locale if specified by customization
    // and has not been set yet. We cannot call
    // chromeos::LanguageSwitchMenu::SwitchLanguage here before
    // EmitLoginPromptReady.
    const std::string current_locale =
        g_browser_process->local_state()->GetString(
            prefs::kApplicationLocale);
    LOG(INFO) << "current locale: " << current_locale;
    if (current_locale.empty()) {
      locale = controller->GetCustomization()->initial_locale();
      LOG(INFO) << "initial locale: " << locale;
      if (!locale.empty()) {
        ResourceBundle::ReloadSharedInstance(locale);
      }
    }
  }

  controller->ShowBackground(screen_bounds);
  controller->Init(first_screen_name, screen_bounds);
  controller->Show();

  if (chromeos::CrosLibrary::Get()->EnsureLoaded())
    chromeos::CrosLibrary::Get()->GetLoginLibrary()->EmitLoginPromptReady();

  if (controller->GetCustomization()) {
    if (!locale.empty())
      chromeos::LanguageSwitchMenu::SwitchLanguage(locale);

    // Set initial timezone if specified by customization.
    const std::string timezone_name =
        controller->GetCustomization()->initial_timezone();
    LOG(INFO) << "initial time zone: " << timezone_name;
    // Apply locale customizations only once so preserve whatever locale
    // user has changed to during OOBE.
    if (!timezone_name.empty()) {
      icu::TimeZone* timezone = icu::TimeZone::createTimeZone(
          icu::UnicodeString::fromUTF8(timezone_name));
      chromeos::CrosLibrary::Get()->GetSystemLibrary()->SetTimezone(timezone);
    }
  }
}

}  // namespace browser
