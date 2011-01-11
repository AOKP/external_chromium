// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/network_message_observer.h"

#include "app/l10n_util.h"
#include "base/callback.h"
#include "base/stl_util-inl.h"
#include "base/string_number_conversions.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/browser_list.h"
#include "chrome/browser/browser_window.h"
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/cros/network_library.h"
#include "chrome/browser/chromeos/notifications/balloon_view_host.h"
#include "chrome/browser/chromeos/options/network_config_view.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/views/window.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/time_format.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "views/window/dialog_delegate.h"
#include "views/window/window.h"

namespace {

// Returns prefs::kShowPlanNotifications in the profile of the last active
// browser. If there is no active browser, returns true.
bool ShouldShowMobilePlanNotifications() {
  Browser* browser = BrowserList::GetLastActive();
  if (!browser || !browser->profile())
    return true;

  PrefService* prefs = browser->profile()->GetPrefs();
  return prefs->GetBoolean(prefs::kShowPlanNotifications);
}

}  // namespace

namespace chromeos {

NetworkMessageObserver::NetworkMessageObserver(Profile* profile)
    : initialized_(false),
      notification_connection_error_(profile, "network_connection.chromeos",
          IDR_NOTIFICATION_NETWORK_FAILED,
          l10n_util::GetStringUTF16(IDS_NETWORK_CONNECTION_ERROR_TITLE)),
      notification_low_data_(profile, "network_low_data.chromeos",
          IDR_NOTIFICATION_BARS_CRITICAL,
          l10n_util::GetStringUTF16(IDS_NETWORK_LOW_DATA_TITLE)),
      notification_no_data_(profile, "network_no_data.chromeos",
          IDR_NOTIFICATION_BARS_EMPTY,
          l10n_util::GetStringUTF16(IDS_NETWORK_OUT_OF_DATA_TITLE)) {
  NetworkLibrary* netlib = CrosLibrary::Get()->GetNetworkLibrary();
  OnNetworkManagerChanged(netlib);
  // Note that this gets added as a NetworkManagerObserver and a
  // CellularDataPlanObserver in browser_init.cc
  initialized_ = true;
}

NetworkMessageObserver::~NetworkMessageObserver() {
  NetworkLibrary* netlib = CrosLibrary::Get()->GetNetworkLibrary();
  netlib->RemoveNetworkManagerObserver(this);
  netlib->RemoveCellularDataPlanObserver(this);
  notification_connection_error_.Hide();
  notification_low_data_.Hide();
  notification_no_data_.Hide();
  STLDeleteValues(&cellular_networks_);
  STLDeleteValues(&wifi_networks_);
}

void NetworkMessageObserver::CreateModalPopup(views::WindowDelegate* view) {
  Browser* browser = BrowserList::GetLastActive();
  if (browser && browser->type() != Browser::TYPE_NORMAL) {
    browser = BrowserList::FindBrowserWithType(browser->profile(),
                                               Browser::TYPE_NORMAL,
                                               true);
  }
  DCHECK(browser);
  views::Window* window = browser::CreateViewsWindow(
      browser->window()->GetNativeHandle(), gfx::Rect(), view);
  window->SetIsAlwaysOnTop(true);
  window->Show();
}

void NetworkMessageObserver::OpenMobileSetupPage(const ListValue* args) {
  Browser* browser = BrowserList::GetLastActive();
  if (browser)
    browser->OpenMobilePlanTabAndActivate();
}

void NetworkMessageObserver::OpenMoreInfoPage(const ListValue* args) {
  Browser* browser = BrowserList::GetLastActive();
  if (!browser)
    return;
  chromeos::NetworkLibrary* lib =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  chromeos::CellularNetwork* cellular = lib->cellular_network();
  if (!cellular)
    return;
  browser->ShowSingletonTab(GURL(cellular->payment_url()), false);
}

void NetworkMessageObserver::OnNetworkManagerChanged(NetworkLibrary* obj) {
  const WifiNetworkVector& wifi_networks = obj->wifi_networks();
  const CellularNetworkVector& cellular_networks = obj->cellular_networks();

  NetworkConfigView* view = NULL;
  std::string new_failed_network;
  // Check to see if we have any newly failed wifi network.
  for (WifiNetworkVector::const_iterator it = wifi_networks.begin();
       it < wifi_networks.end(); it++) {
    const WifiNetwork* wifi = *it;
    if (wifi->failed()) {
      ServicePathWifiMap::iterator iter =
          wifi_networks_.find(wifi->service_path());
      // If the network did not previously exist, then don't do anything.
      // For example, if the user travels to a location and finds a service
      // that has previously failed, we don't want to show a notification.
      if (iter == wifi_networks_.end())
        continue;

      const WifiNetwork* wifi_old = iter->second;
      // If this network was in a failed state previously, then it's not new.
      if (wifi_old->failed())
        continue;

      // Display login box again for bad_passphrase and bad_wepkey errors.
      if (wifi->error() == ERROR_BAD_PASSPHRASE ||
          wifi->error() == ERROR_BAD_WEPKEY) {
        // The NetworkConfigView will show the appropriate error message.
        view = new NetworkConfigView(wifi, true);
        // There should only be one wifi network that failed to connect.
        // If for some reason, we have more than one failure,
        // we only display the first one. So we break here.
        break;
      }

      // If network connection failed, display a notification.
      // We only do this if we were trying to make a new connection.
      // So if a previously connected network got disconnected for any reason,
      // we don't display notification.
      if (wifi_old->connecting()) {
        new_failed_network = wifi->name();
        // Like above, there should only be one newly failed network.
        break;
      }
    }

    // If we find any network connecting, we hide the error notification.
    if (wifi->connecting()) {
      notification_connection_error_.Hide();
    }
  }

  // Refresh stored networks.
  STLDeleteValues(&wifi_networks_);
  wifi_networks_.clear();
  for (WifiNetworkVector::const_iterator it = wifi_networks.begin();
       it < wifi_networks.end(); it++) {
    const WifiNetwork* wifi = *it;
    wifi_networks_[wifi->service_path()] = new WifiNetwork(*wifi);
  }

  STLDeleteValues(&cellular_networks_);
  cellular_networks_.clear();
  for (CellularNetworkVector::const_iterator it = cellular_networks.begin();
       it < cellular_networks.end(); it++) {
    const CellularNetwork* cellular = *it;
    cellular_networks_[cellular->service_path()] =
        new CellularNetwork(*cellular);
  }

  // Show connection error notification if necessary.
  if (!new_failed_network.empty()) {
    // Hide if already shown to force show it in case user has closed it.
    if (notification_connection_error_.visible())
      notification_connection_error_.Hide();
    notification_connection_error_.Show(l10n_util::GetStringFUTF16(
        IDS_NETWORK_CONNECTION_ERROR_MESSAGE,
        ASCIIToUTF16(new_failed_network)), false, false);
  }

  // Show login box if necessary.
  if (view && initialized_)
    CreateModalPopup(view);
}

void NetworkMessageObserver::OnCellularDataPlanChanged(NetworkLibrary* obj) {
  const CellularNetwork* cellular = obj->cellular_network();
  if (!cellular)
    return;
  const CellularDataPlan* plan = cellular->GetSignificantDataPlan();
  std::string new_plan_name = plan ? plan->plan_name : "";
  CellularDataPlanType new_plan_type = plan ? plan->plan_type :
                                              CELLULAR_DATA_PLAN_UNKNOWN;

  // If connected cellular network changed, or data plan is different, then
  // it's a new network. Then hide all previous notifications.
  bool new_plan = false;
  if (cellular->service_path() != cellular_service_path_) {
    cellular_service_path_ = cellular->service_path();
    new_plan = true;
  } else if (new_plan_name != cellular_data_plan_name_ ||
      new_plan_type != cellular_data_plan_type_) {
    new_plan = true;
  }

  cellular_data_plan_name_ = new_plan_name;
  cellular_data_plan_type_ = new_plan_type;

  bool should_show_notifications = ShouldShowMobilePlanNotifications();
  if (!should_show_notifications) {
    notification_low_data_.Hide();
    notification_no_data_.Hide();
    return;
  }

  if (new_plan) {
    notification_low_data_.Hide();
    notification_no_data_.Hide();
    if (!plan && cellular->needs_new_plan()) {
      notification_no_data_.set_title(
          l10n_util::GetStringFUTF16(IDS_NETWORK_NO_DATA_PLAN_TITLE,
                                     UTF8ToUTF16(cellular->service_name())));
      notification_no_data_.Show(
          l10n_util::GetStringFUTF16(
              IDS_NETWORK_NO_DATA_PLAN_MESSAGE,
              UTF8ToUTF16(cellular->service_name())),
          l10n_util::GetStringUTF16(IDS_NETWORK_PURCHASE_MORE_MESSAGE),
          NewCallback(this, &NetworkMessageObserver::OpenMobileSetupPage),
          false, false);
      return;
    } else if (cellular_data_plan_type_ == CELLULAR_DATA_PLAN_UNLIMITED) {
      notification_no_data_.set_title(
          l10n_util::GetStringFUTF16(IDS_NETWORK_DATA_EXPIRED_TITLE,
                                     ASCIIToUTF16(cellular_data_plan_name_)));
      notification_low_data_.set_title(
          l10n_util::GetStringFUTF16(IDS_NETWORK_NEARING_EXPIRATION_TITLE,
                                     ASCIIToUTF16(cellular_data_plan_name_)));
    } else {
      notification_no_data_.set_title(
          l10n_util::GetStringFUTF16(IDS_NETWORK_OUT_OF_DATA_TITLE,
                                     ASCIIToUTF16(cellular_data_plan_name_)));
      notification_low_data_.set_title(
          l10n_util::GetStringFUTF16(IDS_NETWORK_LOW_DATA_TITLE,
                                     ASCIIToUTF16(cellular_data_plan_name_)));
    }
  }

  if (cellular_data_plan_type_ == CELLULAR_DATA_PLAN_UNKNOWN)
    return;

  if (cellular->GetDataLeft() == CellularNetwork::DATA_NONE) {
    notification_low_data_.Hide();
    string16 message =
        cellular_data_plan_type_ == CELLULAR_DATA_PLAN_UNLIMITED ?
            TimeFormat::TimeRemaining(base::TimeDelta()) :
            l10n_util::GetStringFUTF16(IDS_NETWORK_DATA_REMAINING_MESSAGE,
                                       ASCIIToUTF16("0"));
    notification_no_data_.Show(message,
        l10n_util::GetStringUTF16(IDS_NETWORK_PURCHASE_MORE_MESSAGE),
        NewCallback(this, &NetworkMessageObserver::OpenMobileSetupPage),
        false, false);
  } else if (cellular->GetDataLeft() == CellularNetwork::DATA_VERY_LOW) {
    notification_no_data_.Hide();
    string16 message =
        cellular_data_plan_type_ == CELLULAR_DATA_PLAN_UNLIMITED ?
            plan->GetPlanExpiration() :
            l10n_util::GetStringFUTF16(IDS_NETWORK_DATA_REMAINING_MESSAGE,
                 UTF8ToUTF16(base::Int64ToString(plan->remaining_mbytes())));
    notification_low_data_.Show(message,
        l10n_util::GetStringUTF16(IDS_NETWORK_MORE_INFO_MESSAGE),
        NewCallback(this, &NetworkMessageObserver::OpenMoreInfoPage),
        false, false);
  } else {
    // Got data, so hide notifications.
    notification_low_data_.Hide();
    notification_no_data_.Hide();
  }
}

}  // namespace chromeos
