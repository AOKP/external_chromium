// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/dom_ui/internet_options_handler.h"

#include <ctype.h>

#include <string>
#include <vector>

#include "app/l10n_util.h"
#include "app/resource_bundle.h"
#include "base/base64.h"
#include "base/basictypes.h"
#include "base/callback.h"
#include "base/i18n/time_formatting.h"
#include "base/string16.h"
#include "base/string_number_conversions.h"
#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_list.h"
#include "chrome/browser/browser_window.h"
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/login/ownership_service.h"
#include "chrome/browser/chromeos/status/network_menu.h"
#include "chrome/browser/dom_ui/dom_ui_util.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/browser/tab_contents/tab_contents_view.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/time_format.h"
#include "grit/browser_resources.h"
#include "grit/chromium_strings.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"
#include "grit/theme_resources.h"
#include "third_party/skia/include/core/SkBitmap.h"

static const char kOtherNetworksFakePath[] = "?";

namespace {

// Format the hardware address like "0011AA22BB33" => "00:11:AA:22:BB:33".
std::string FormatHardwareAddress(const std::string& address) {
  std::string output;
  for (size_t i = 0; i < address.size(); ++i) {
    if (i != 0 && i % 2 == 0) {
      output.push_back(':');
    }
    output.push_back(toupper(address[i]));
  }
  return output;
}

}  // namespace

InternetOptionsHandler::InternetOptionsHandler() {
  chromeos::NetworkLibrary* netlib =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  netlib->AddNetworkManagerObserver(this);
  netlib->AddCellularDataPlanObserver(this);
  MonitorActiveNetwork(netlib);
}

InternetOptionsHandler::~InternetOptionsHandler() {
  chromeos::NetworkLibrary *netlib =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  netlib->RemoveNetworkManagerObserver(this);
  netlib->RemoveCellularDataPlanObserver(this);
  netlib->RemoveObserverForAllNetworks(this);
}

void InternetOptionsHandler::GetLocalizedValues(
    DictionaryValue* localized_strings) {
  DCHECK(localized_strings);
  // Internet page - ChromeOS
  localized_strings->SetString("internetPage",
      l10n_util::GetStringUTF16(IDS_OPTIONS_INTERNET_TAB_LABEL));

  localized_strings->SetString("wired_title",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_SECTION_TITLE_WIRED_NETWORK));
  localized_strings->SetString("wireless_title",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_SECTION_TITLE_WIRELESS_NETWORK));
  localized_strings->SetString("remembered_title",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_SECTION_TITLE_REMEMBERED_NETWORK));

  localized_strings->SetString("connect_button",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_CONNECT));
  localized_strings->SetString("disconnect_button",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_DISCONNECT));
  localized_strings->SetString("options_button",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_OPTIONS));
  localized_strings->SetString("forget_button",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_FORGET));
  localized_strings->SetString("activate_button",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_ACTIVATE));
  localized_strings->SetString("buyplan_button",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_BUY_PLAN));

  localized_strings->SetString("wifiNetworkTabLabel",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_TAB_WIFI));
  localized_strings->SetString("cellularPlanTabLabel",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_TAB_PLAN));
  localized_strings->SetString("cellularConnTabLabel",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_TAB_CONNECTION));
  localized_strings->SetString("cellularDeviceTabLabel",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_TAB_DEVICE));
  localized_strings->SetString("networkTabLabel",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_TAB_NETWORK));

  localized_strings->SetString("connectionState",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CONNECTION_STATE));
  localized_strings->SetString("inetAddress",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_ADDRESS));
  localized_strings->SetString("inetSubnetAddress",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SUBNETMASK));
  localized_strings->SetString("inetGateway",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_GATEWAY));
  localized_strings->SetString("inetDns",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_DNSSERVER));
  localized_strings->SetString("hardwareAddress",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_HARDWARE_ADDRESS));

  localized_strings->SetString("inetSsid",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_NETWORK_ID));
  localized_strings->SetString("inetIdent",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_CERT_IDENTITY));
  localized_strings->SetString("inetCert",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_CERT));
  localized_strings->SetString("inetCertPass",
      l10n_util::GetStringUTF16(
           IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_PRIVATE_KEY_PASSWORD));
  localized_strings->SetString("inetPassProtected",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_NET_PROTECTED));
  localized_strings->SetString("inetAutoConnectNetwork",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_AUTO_CONNECT));
  localized_strings->SetString("inetCertPkcs",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_CERT_INSTALLED));
  localized_strings->SetString("inetLogin",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_LOGIN));
  localized_strings->SetString("inetShowPass",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SHOWPASSWORD));
  localized_strings->SetString("inetSecurityNone",
      l10n_util::GetStringFUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SECURITY_SELECT,
          l10n_util::GetStringUTF16(
              IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SECURITY_NONE)));
  localized_strings->SetString("inetSecurityWEP",
      l10n_util::GetStringFUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SECURITY_SELECT,
          l10n_util::GetStringUTF16(
              IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SECURITY_WEP)));
  localized_strings->SetString("inetSecurityWPA",
      l10n_util::GetStringFUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SECURITY_SELECT,
          l10n_util::GetStringUTF16(
              IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SECURITY_WPA)));
  localized_strings->SetString("inetSecurityRSN",
      l10n_util::GetStringFUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SECURITY_SELECT,
          l10n_util::GetStringUTF16(
              IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SECURITY_RSN)));
  localized_strings->SetString("inetPassPrompt",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_PASSWORD));
  localized_strings->SetString("inetSsidPrompt",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SSID));
  localized_strings->SetString("inetStatus",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_STATUS_TITLE));
  localized_strings->SetString("inetConnect",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_CONNECT_TITLE));

  localized_strings->SetString("serviceName",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_SERVICE_NAME));
  localized_strings->SetString("networkTechnology",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_NETWORK_TECHNOLOGY));
  localized_strings->SetString("operatorName",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_OPERATOR));
  localized_strings->SetString("operatorCode",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_OPERATOR_CODE));
  localized_strings->SetString("activationState",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_ACTIVATION_STATE));
  localized_strings->SetString("roamingState",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_ROAMING_STATE));
  localized_strings->SetString("restrictedPool",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_RESTRICTED_POOL));
  localized_strings->SetString("errorState",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_ERROR_STATE));
  localized_strings->SetString("manufacturer",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_MANUFACTURER));
  localized_strings->SetString("modelId",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_MODEL_ID));
  localized_strings->SetString("firmwareRevision",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_FIRMWARE_REVISION));
  localized_strings->SetString("hardwareRevision",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_HARDWARE_REVISION));
  localized_strings->SetString("lastUpdate",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_LAST_UPDATE));
  localized_strings->SetString("prlVersion",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CELLULAR_PRL_VERSION));

  localized_strings->SetString("planLoading",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_LOADING_PLAN));
  localized_strings->SetString("purchaseMore",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_PURCHASE_MORE));
  localized_strings->SetString("moreInfo",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_MORE_INFO));
  localized_strings->SetString("dataRemaining",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_DATA_REMAINING));
  localized_strings->SetString("planExpires",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_EXPIRES));
  localized_strings->SetString("showPlanNotifications",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_SHOW_MOBILE_NOTIFICATION));
  localized_strings->SetString("autoconnectCellular",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_AUTO_CONNECT));
  localized_strings->SetString("customerSupport",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_OPTIONS_CUSTOMER_SUPPORT));

  localized_strings->SetString("enableWifi",
      l10n_util::GetStringFUTF16(
          IDS_STATUSBAR_NETWORK_DEVICE_ENABLE,
          l10n_util::GetStringUTF16(IDS_STATUSBAR_NETWORK_DEVICE_WIFI)));
  localized_strings->SetString("disableWifi",
      l10n_util::GetStringFUTF16(
          IDS_STATUSBAR_NETWORK_DEVICE_DISABLE,
          l10n_util::GetStringUTF16(IDS_STATUSBAR_NETWORK_DEVICE_WIFI)));
  localized_strings->SetString("enableCellular",
      l10n_util::GetStringFUTF16(
          IDS_STATUSBAR_NETWORK_DEVICE_ENABLE,
          l10n_util::GetStringUTF16(IDS_STATUSBAR_NETWORK_DEVICE_CELLULAR)));
  localized_strings->SetString("disableCellular",
      l10n_util::GetStringFUTF16(
          IDS_STATUSBAR_NETWORK_DEVICE_DISABLE,
          l10n_util::GetStringUTF16(IDS_STATUSBAR_NETWORK_DEVICE_CELLULAR)));
  localized_strings->SetString("generalNetworkingTitle",
      l10n_util::GetStringUTF16(
          IDS_OPTIONS_SETTINGS_INTERNET_CONTROL_TITLE));

  localized_strings->SetString("detailsInternetOk",
      l10n_util::GetStringUTF16(IDS_OK));
  localized_strings->SetString("detailsInternetDismiss",
      l10n_util::GetStringUTF16(IDS_CANCEL));

  localized_strings->Set("wiredList", GetWiredList());
  localized_strings->Set("wirelessList", GetWirelessList());
  localized_strings->Set("rememberedList", GetRememberedList());

  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  localized_strings->SetBoolean("wifiAvailable", cros->wifi_available());
  localized_strings->SetBoolean("wifiEnabled", cros->wifi_enabled());
  localized_strings->SetBoolean("cellularAvailable",
                                cros->cellular_available());
  localized_strings->SetBoolean("cellularEnabled", cros->cellular_enabled());
}

void InternetOptionsHandler::RegisterMessages() {
  // Setup handlers specific to this panel.
  DCHECK(dom_ui_);
  dom_ui_->RegisterMessageCallback("buttonClickCallback",
      NewCallback(this, &InternetOptionsHandler::ButtonClickCallback));
  dom_ui_->RegisterMessageCallback("refreshCellularPlan",
      NewCallback(this, &InternetOptionsHandler::RefreshCellularPlanCallback));
  dom_ui_->RegisterMessageCallback("loginToNetwork",
      NewCallback(this, &InternetOptionsHandler::LoginCallback));
  dom_ui_->RegisterMessageCallback("loginToCertNetwork",
      NewCallback(this, &InternetOptionsHandler::LoginCertCallback));
  dom_ui_->RegisterMessageCallback("setDetails",
      NewCallback(this, &InternetOptionsHandler::SetDetailsCallback));
  dom_ui_->RegisterMessageCallback("loginToOtherNetwork",
      NewCallback(this, &InternetOptionsHandler::LoginToOtherCallback));
  dom_ui_->RegisterMessageCallback("enableWifi",
      NewCallback(this, &InternetOptionsHandler::EnableWifiCallback));
  dom_ui_->RegisterMessageCallback("disableWifi",
      NewCallback(this, &InternetOptionsHandler::DisableWifiCallback));
  dom_ui_->RegisterMessageCallback("enableCellular",
      NewCallback(this, &InternetOptionsHandler::EnableCellularCallback));
  dom_ui_->RegisterMessageCallback("disableCellular",
      NewCallback(this, &InternetOptionsHandler::DisableCellularCallback));
  dom_ui_->RegisterMessageCallback("buyDataPlan",
      NewCallback(this, &InternetOptionsHandler::BuyDataPlanCallback));
  dom_ui_->RegisterMessageCallback("showMorePlanInfo",
      NewCallback(this, &InternetOptionsHandler::BuyDataPlanCallback));
}

void InternetOptionsHandler::EnableWifiCallback(const ListValue* args) {
  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  cros->EnableWifiNetworkDevice(true);
}

void InternetOptionsHandler::DisableWifiCallback(const ListValue* args) {
  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  cros->EnableWifiNetworkDevice(false);
}

void InternetOptionsHandler::EnableCellularCallback(const ListValue* args) {
  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  cros->EnableCellularNetworkDevice(true);
}

void InternetOptionsHandler::DisableCellularCallback(const ListValue* args) {
  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  cros->EnableCellularNetworkDevice(false);
}

void InternetOptionsHandler::BuyDataPlanCallback(const ListValue* args) {
  if (!dom_ui_)
    return;
  Browser* browser = BrowserList::FindBrowserWithFeature(
      dom_ui_->GetProfile(), Browser::FEATURE_TABSTRIP);
  if (browser)
    browser->OpenMobilePlanTabAndActivate();
}

void InternetOptionsHandler::RefreshNetworkData(
    chromeos::NetworkLibrary* cros) {
  DictionaryValue dictionary;
  dictionary.Set("wiredList", GetWiredList());
  dictionary.Set("wirelessList", GetWirelessList());
  dictionary.Set("rememberedList", GetRememberedList());
  dictionary.SetBoolean("wifiAvailable", cros->wifi_available());
  dictionary.SetBoolean("wifiEnabled", cros->wifi_enabled());
  dictionary.SetBoolean("cellularAvailable", cros->cellular_available());
  dictionary.SetBoolean("cellularEnabled", cros->cellular_enabled());
  dom_ui_->CallJavascriptFunction(
      L"options.InternetOptions.refreshNetworkData", dictionary);
}

void InternetOptionsHandler::OnNetworkManagerChanged(
    chromeos::NetworkLibrary* cros) {
  if (!dom_ui_)
    return;
  MonitorActiveNetwork(cros);
  RefreshNetworkData(cros);
}

void InternetOptionsHandler::OnNetworkChanged(
    chromeos::NetworkLibrary* cros,
    const chromeos::Network* network) {
  if (dom_ui_)
    RefreshNetworkData(cros);
}

// Add an observer for the active network, if any, so
// that we can dynamically display the correct icon for
// that network's signal strength.
// TODO(ers) Ideally, on this page we'd monitor all networks for
// signal strength changes, not just the active network.
void InternetOptionsHandler::MonitorActiveNetwork(
    chromeos::NetworkLibrary* cros) {
  const chromeos::Network* network = cros->active_network();
  if (active_network_.empty() || network == NULL ||
      active_network_ != network->service_path()) {
    if (!active_network_.empty()) {
      cros->RemoveNetworkObserver(active_network_, this);
    }
    if (network != NULL) {
      cros->AddNetworkObserver(network->service_path(), this);
    }
  }
  if (network != NULL)
    active_network_ = network->service_path();
  else
    active_network_ = "";
}

void InternetOptionsHandler::OnCellularDataPlanChanged(
    chromeos::NetworkLibrary* obj) {
  if (!dom_ui_)
    return;
  chromeos::CellularNetwork* cellular = obj->cellular_network();
  if (!cellular)
    return;
  const chromeos::CellularDataPlanVector& plans = cellular->GetDataPlans();
  DictionaryValue connection_plans;
  ListValue* plan_list = new ListValue();
  for (chromeos::CellularDataPlanVector::const_iterator iter = plans.begin();
       iter != plans.end();
       ++iter) {
    plan_list->Append(CellularDataPlanToDictionary(*iter));
  }
  connection_plans.SetString("servicePath", cellular->service_path());
  connection_plans.Set("plans", plan_list);
  dom_ui_->CallJavascriptFunction(
      L"options.InternetOptions.updateCellularPlans", connection_plans);
}

DictionaryValue* InternetOptionsHandler::CellularDataPlanToDictionary(
    const chromeos::CellularDataPlan& plan) {

  DictionaryValue* plan_dict = new DictionaryValue();
  plan_dict->SetInteger("plan_type", plan.plan_type);
  plan_dict->SetString("name", plan.plan_name);
  plan_dict->SetString("planSummary", plan.GetPlanDesciption());
  plan_dict->SetString("dataRemaining", plan.GetDataRemainingDesciption());
  plan_dict->SetString("planExpires", plan.GetPlanExpiration());
  plan_dict->SetString("warning", plan.GetRemainingWarning());
  return plan_dict;
}

void InternetOptionsHandler::SetDetailsCallback(const ListValue* args) {
  std::string service_path;
  std::string auto_connect_str;

  if (args->GetSize() < 2 ||
      !args->GetString(0, &service_path) ||
      !args->GetString(1, &auto_connect_str)) {
    NOTREACHED();
    return;
  }

  if (!chromeos::OwnershipService::GetSharedInstance()->CurrentUserIsOwner()) {
    LOG(WARNING) << "Non-owner tried to change a network.";
    return;
  }

  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  chromeos::WifiNetwork* network = cros->FindWifiNetworkByPath(service_path);
  if (!network)
    return;
  bool changed = false;
  if (network->encrypted()) {
    if (network->encrypted() &&
        network->encryption() == chromeos::SECURITY_8021X) {
      std::string ident;
      if (!args->GetString(2, &ident)) {
        NOTREACHED();
        return;
      }
      if (ident != network->identity()) {
        network->set_identity(ident);
        changed = true;
      }
      if (!is_certificate_in_pkcs11(network->cert_path())) {
        std::string certpath;
        if (!args->GetString(3, &certpath)) {
          NOTREACHED();
          return;
        }
        if (certpath != network->cert_path()) {
          network->set_cert_path(certpath);
          changed = true;
        }
      }
    }
  }

  bool auto_connect = auto_connect_str == "true";
  if (auto_connect != network->auto_connect()) {
    network->set_auto_connect(auto_connect);
    changed = true;
  }

  if (changed)
    cros->SaveWifiNetwork(network);
}

// Parse 'path' to determine if the certificate is stored in a pkcs#11 device.
// flimflam recognizes the string "SETTINGS:" to specify authentication
// parameters. 'key_id=' indicates that the certificate is stored in a pkcs#11
// device. See src/third_party/flimflam/files/doc/service-api.txt.
bool InternetOptionsHandler::is_certificate_in_pkcs11(const std::string& path) {
  static const std::string settings_string("SETTINGS:");
  static const std::string pkcs11_key("key_id");
  if (path.find(settings_string) == 0) {
    std::string::size_type idx = path.find(pkcs11_key);
    if (idx != std::string::npos)
      idx = path.find_first_not_of(kWhitespaceASCII, idx + pkcs11_key.length());
    if (idx != std::string::npos && path[idx] == '=')
      return true;
  }
  return false;
}

void InternetOptionsHandler::PopulateDictionaryDetails(
    const chromeos::Network* net, chromeos::NetworkLibrary* cros) {
  DCHECK(net);
  DictionaryValue dictionary;
  chromeos::ConnectionType type = net->type();
  std::string hardware_address;
  chromeos::NetworkIPConfigVector ipconfigs =
      cros->GetIPConfigs(net->device_path(), &hardware_address);
  scoped_ptr<ListValue> ipconfig_list(new ListValue());
  for (chromeos::NetworkIPConfigVector::const_iterator it = ipconfigs.begin();
       it != ipconfigs.end(); ++it) {
    scoped_ptr<DictionaryValue> ipconfig_dict(new DictionaryValue());
    const chromeos::NetworkIPConfig& ipconfig = *it;
    ipconfig_dict->SetString("address", ipconfig.address);
    ipconfig_dict->SetString("subnetAddress", ipconfig.netmask);
    ipconfig_dict->SetString("gateway", ipconfig.gateway);
    ipconfig_dict->SetString("dns", ipconfig.name_servers);
    ipconfig_list->Append(ipconfig_dict.release());
  }
  dictionary.Set("ipconfigs", ipconfig_list.release());
  dictionary.SetInteger("type", type);
  dictionary.SetString("servicePath", net->service_path());
  dictionary.SetBoolean("connecting", net->connecting());
  dictionary.SetBoolean("connected", net->connected());
  dictionary.SetString("connectionState", net->GetStateString());
  if (type == chromeos::TYPE_WIFI) {
    chromeos::WifiNetwork* wireless =
        cros->FindWifiNetworkByPath(net->service_path());
    if (!wireless) {
      LOG(WARNING) << "Cannot find network " << net->service_path();
    } else {
      dictionary.SetString("ssid", wireless->name());
      dictionary.SetBoolean("autoConnect", wireless->auto_connect());
      if (wireless->encrypted()) {
        dictionary.SetBoolean("encrypted", true);
        if (wireless->encryption() == chromeos::SECURITY_8021X) {
          bool certificate_in_pkcs11 =
              is_certificate_in_pkcs11(wireless->cert_path());
          if (certificate_in_pkcs11) {
            dictionary.SetBoolean("certInPkcs", true);
          } else {
            dictionary.SetBoolean("certInPkcs", false);
          }
          dictionary.SetString("certPath", wireless->cert_path());
          dictionary.SetString("ident", wireless->identity());
          dictionary.SetBoolean("certNeeded", true);
          dictionary.SetString("certPass", wireless->passphrase());
        } else {
          dictionary.SetBoolean("certNeeded", false);
        }
      } else {
        dictionary.SetBoolean("encrypted", false);
      }
    }
  } else if (type == chromeos::TYPE_CELLULAR) {
    chromeos::CellularNetwork* cellular =
        cros->FindCellularNetworkByPath(net->service_path());
    if (!cellular) {
      LOG(WARNING) << "Cannot find network " << net->service_path();
    } else {
      // Cellular network / connection settings.
      dictionary.SetString("serviceName", cellular->service_name());
      dictionary.SetString("networkTechnology",
                           cellular->GetNetworkTechnologyString());
      dictionary.SetString("operatorName", cellular->operator_name());
      dictionary.SetString("operatorCode", cellular->operator_code());
      dictionary.SetString("activationState",
                           cellular->GetActivationStateString());
      dictionary.SetString("roamingState",
                           cellular->GetRoamingStateString());
      dictionary.SetString("restrictedPool",
          cellular->restricted_pool() ?
            l10n_util::GetStringUTF8(IDS_CONFIRM_MESSAGEBOX_YES_BUTTON_LABEL) :
            l10n_util::GetStringUTF8(IDS_CONFIRM_MESSAGEBOX_NO_BUTTON_LABEL));
      dictionary.SetString("errorState", cellular->GetErrorString());
      dictionary.SetString("supportUrl",
                           cellular->payment_url());
      // Device settings.
      dictionary.SetString("manufacturer", cellular->manufacturer());
      dictionary.SetString("modelId", cellular->model_id());
      dictionary.SetString("firmwareRevision", cellular->firmware_revision());
      dictionary.SetString("hardwareRevision", cellular->hardware_revision());
      dictionary.SetString("lastUpdate", cellular->last_update());
      dictionary.SetString("prlVersion", StringPrintf("%u",
                                                      cellular->prl_version()));
      dictionary.SetString("meid", cellular->meid());
      dictionary.SetString("imei", cellular->imei());
      dictionary.SetString("mdn", cellular->mdn());
      dictionary.SetString("imsi", cellular->imsi());
      dictionary.SetString("esn", cellular->esn());
      dictionary.SetString("min", cellular->min());

      dictionary.SetBoolean("gsm", cellular->is_gsm());
    }
  }
  if (!hardware_address.empty()) {
    dictionary.SetString("hardwareAddress",
                         FormatHardwareAddress(hardware_address));
  }

  dom_ui_->CallJavascriptFunction(
      L"options.InternetOptions.showDetailedInfo", dictionary);
}

void InternetOptionsHandler::LoginCallback(const ListValue* args) {
  std::string service_path;
  std::string password;

  if (args->GetSize() != 2 ||
      !args->GetString(0, &service_path) ||
      !args->GetString(1, &password)) {
    NOTREACHED();
    return;
  }

  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  chromeos::WifiNetwork* network = cros->FindWifiNetworkByPath(service_path);
  if (network) {
    cros->ConnectToWifiNetwork(
        network, password, std::string(), std::string());
  } else {
    // Network disappeared while the user is connecting to it.
    // TODO(chocobo): Display error message.
    LOG(WARNING) << "Cannot find network to connect " << service_path;
  }
}

void InternetOptionsHandler::LoginCertCallback(const ListValue* args) {
  std::string service_path;
  std::string identity;
  std::string certpath;
  if (args->GetSize() < 3 ||
      !args->GetString(0, &service_path) ||
      !args->GetString(1, &certpath) ||
      !args->GetString(2, &identity)) {
    return;
  }
  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  chromeos::WifiNetwork* network =
      cros->FindWifiNetworkByPath(service_path);
  if (!network)
    return;
  // If password does not come from the input, use one saved with the
  // network details.
  std::string password;
  if (args->GetSize() != 4 || !args->GetString(3, &password)) {
    password = network->passphrase();
  }
  cros->ConnectToWifiNetwork(
      network, password, identity, certpath);
}

void InternetOptionsHandler::LoginToOtherCallback(const ListValue* args) {
  std::string security;
  std::string ssid;
  std::string password;

  if (args->GetSize() != 3 ||
      !args->GetString(0, &security) ||
      !args->GetString(1, &ssid) ||
      !args->GetString(2, &password)) {
    NOTREACHED();
    return;
  }

  chromeos::ConnectionSecurity sec = chromeos::SECURITY_UNKNOWN;
  if (security == "none") {
    sec = chromeos::SECURITY_NONE;
  } else if (security == "wep") {
    sec = chromeos::SECURITY_WEP;
  } else if (security == "wpa") {
    sec = chromeos::SECURITY_WPA;
  } else if (security == "rsn") {
    sec = chromeos::SECURITY_RSN;
  }

  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();

  cros->ConnectToWifiNetwork(sec, ssid, password, std::string(), std::string(),
                             true);
}

void InternetOptionsHandler::ButtonClickCallback(const ListValue* args) {
  std::string str_type;
  std::string service_path;
  std::string command;
  if (args->GetSize() != 3 ||
      !args->GetString(0, &str_type) ||
      !args->GetString(1, &service_path) ||
      !args->GetString(2, &command)) {
    NOTREACHED();
    return;
  }

  bool is_owner =
      chromeos::OwnershipService::GetSharedInstance()->CurrentUserIsOwner();

  int type = atoi(str_type.c_str());
  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();

  if (type == chromeos::TYPE_ETHERNET) {
    chromeos::EthernetNetwork* ether = cros->ethernet_network();
    PopulateDictionaryDetails(ether, cros);
  } else if (type == chromeos::TYPE_WIFI) {
    chromeos::WifiNetwork* network;
    if (command == "forget") {
      if (!is_owner) {
        LOG(WARNING) << "Non-owner tried to forget a network.";
        return;
      }
      cros->ForgetWifiNetwork(service_path);
    } else if ((network = cros->FindWifiNetworkByPath(service_path))) {
      if (command == "connect") {
        // Connect to wifi here. Open password page if appropriate.
        if (network->encrypted() && !network->auto_connect()) {
          if (network->encryption() == chromeos::SECURITY_8021X) {
            PopulateDictionaryDetails(network, cros);
          } else {
            DictionaryValue dictionary;
            dictionary.SetString("servicePath", network->service_path());
            dom_ui_->CallJavascriptFunction(
                L"options.InternetOptions.showPasswordEntry", dictionary);
          }
        } else {
          cros->ConnectToWifiNetwork(
              network, std::string(), std::string(), std::string());
        }
      } else if (command == "disconnect") {
        cros->DisconnectFromWirelessNetwork(network);
      } else if (command == "options") {
        PopulateDictionaryDetails(network, cros);
      }
    }
  } else if (type == chromeos::TYPE_CELLULAR) {
    chromeos::CellularNetwork* cellular =
        cros->FindCellularNetworkByPath(service_path);
    if (cellular) {
      if (command == "connect") {
        cros->ConnectToCellularNetwork(cellular);
      } else if (command == "disconnect") {
        cros->DisconnectFromWirelessNetwork(cellular);
      } else if (command == "activate") {
        Browser* browser = BrowserList::GetLastActive();
        if (browser)
          browser->OpenMobilePlanTabAndActivate();
      } else if (command == "options") {
        PopulateDictionaryDetails(cellular, cros);
      }
    }
  } else {
    NOTREACHED();
  }
}

void InternetOptionsHandler::RefreshCellularPlanCallback(
    const ListValue* args) {
  std::string service_path;
  if (args->GetSize() != 1 ||
      !args->GetString(0, &service_path)) {
    NOTREACHED();
    return;
  }
  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  chromeos::CellularNetwork* cellular =
      cros->FindCellularNetworkByPath(service_path);
  if (cellular)
    cros->RefreshCellularDataPlans(cellular);
}

ListValue* InternetOptionsHandler::GetNetwork(const std::string& service_path,
    const SkBitmap& icon, const std::string& name, bool connecting,
    bool connected, bool connectable, chromeos::ConnectionType connection_type,
    bool remembered, chromeos::ActivationState activation_state,
    bool restricted_ip) {
  ListValue* network = new ListValue();

  int connection_state = IDS_STATUSBAR_NETWORK_DEVICE_DISCONNECTED;
  if (!connectable)
    connection_state = IDS_STATUSBAR_NETWORK_DEVICE_NOT_CONFIGURED;
  else if (connecting)
    connection_state = IDS_STATUSBAR_NETWORK_DEVICE_CONNECTING;
  else if (connected)
    connection_state = IDS_STATUSBAR_NETWORK_DEVICE_CONNECTED;
  std::string status = l10n_util::GetStringUTF8(connection_state);
  if (connection_type == chromeos::TYPE_CELLULAR) {
    if (activation_state == chromeos::ACTIVATION_STATE_ACTIVATED &&
        restricted_ip && connected) {
      status = l10n_util::GetStringUTF8(IDS_OPTIONS_SETTINGS_NO_PLAN_LABEL);
    } else if (activation_state != chromeos::ACTIVATION_STATE_ACTIVATED) {
      status.append(" / ");
      status.append(
          chromeos::CellularNetwork::ActivationStateToString(activation_state));
    }
  }
  // service path
  network->Append(Value::CreateStringValue(service_path));
  // name
  network->Append(Value::CreateStringValue(name));
  // status
  network->Append(Value::CreateStringValue(status));
  // type
  network->Append(Value::CreateIntegerValue(static_cast<int>(connection_type)));
  // connected
  network->Append(Value::CreateBooleanValue(connected));
  // connecting
  network->Append(Value::CreateBooleanValue(connecting));
  // icon data url
  network->Append(Value::CreateStringValue(icon.isNull() ? "" :
      dom_ui_util::GetImageDataUrl(icon)));
  // remembered
  network->Append(Value::CreateBooleanValue(remembered));
  // activation_state
  network->Append(Value::CreateIntegerValue(
                    static_cast<int>(activation_state)));
  // restricted
  network->Append(Value::CreateBooleanValue(restricted_ip));
  // connectable
  network->Append(Value::CreateBooleanValue(connectable));
  return network;
}

ListValue* InternetOptionsHandler::GetWiredList() {
  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  ListValue* list = new ListValue();

  // If ethernet is not enabled, then don't add anything.
  if (cros->ethernet_enabled()) {
    chromeos::EthernetNetwork* ethernet_network =
        cros->ethernet_network();
    SkBitmap icon = *rb.GetBitmapNamed(IDR_STATUSBAR_WIRED_BLACK);
    if (!ethernet_network || (!ethernet_network->connecting() &&
        !ethernet_network->connected())) {
      icon = chromeos::NetworkMenu::IconForDisplay(icon,
          *rb.GetBitmapNamed(IDR_STATUSBAR_NETWORK_DISCONNECTED));
    }
    if (ethernet_network) {
      list->Append(GetNetwork(
          ethernet_network->service_path(),
          icon,
          l10n_util::GetStringUTF8(IDS_STATUSBAR_NETWORK_DEVICE_ETHERNET),
          ethernet_network->connecting(),
          ethernet_network->connected(),
          ethernet_network->connectable(),
          chromeos::TYPE_ETHERNET,
          false,
          chromeos::ACTIVATION_STATE_UNKNOWN,
          false));
    }
  }
  return list;
}

ListValue* InternetOptionsHandler::GetWirelessList() {
  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  ListValue* list = new ListValue();

  const chromeos::WifiNetworkVector& wifi_networks = cros->wifi_networks();
  for (chromeos::WifiNetworkVector::const_iterator it =
      wifi_networks.begin(); it != wifi_networks.end(); ++it) {
    SkBitmap icon = chromeos::NetworkMenu::IconForNetworkStrength(
        (*it)->strength(), true);
    if ((*it)->encrypted()) {
      icon = chromeos::NetworkMenu::IconForDisplay(icon,
          *rb.GetBitmapNamed(IDR_STATUSBAR_NETWORK_SECURE));
    }
    list->Append(GetNetwork(
        (*it)->service_path(),
        icon,
        (*it)->name(),
        (*it)->connecting(),
        (*it)->connected(),
        (*it)->connectable(),
        chromeos::TYPE_WIFI,
        false,
        chromeos::ACTIVATION_STATE_UNKNOWN,
        false));
  }

  const chromeos::CellularNetworkVector cellular_networks =
      cros->cellular_networks();
  for (chromeos::CellularNetworkVector::const_iterator it =
      cellular_networks.begin(); it != cellular_networks.end(); ++it) {
    SkBitmap icon = chromeos::NetworkMenu::IconForNetworkStrength(
        (*it)->strength(), true);
    SkBitmap badge = chromeos::NetworkMenu::BadgeForNetworkTechnology(*it);
    icon = chromeos::NetworkMenu::IconForDisplay(icon, badge);
    list->Append(GetNetwork(
        (*it)->service_path(),
        icon,
        (*it)->name(),
        (*it)->connecting(),
        (*it)->connected(),
        (*it)->connectable(),
        chromeos::TYPE_CELLULAR,
        false,
        (*it)->activation_state(),
        (*it)->restricted_pool()));
  }

  // Add "Other..." if wifi is enabled.
  if (cros->wifi_enabled()) {
    list->Append(GetNetwork(
        kOtherNetworksFakePath,
        *rb.GetBitmapNamed(IDR_STATUSBAR_NETWORK_BARS0_BLACK),
        l10n_util::GetStringUTF8(IDS_OPTIONS_SETTINGS_OTHER_NETWORKS),
        false,
        false,
        true,
        chromeos::TYPE_WIFI,
        false,
        chromeos::ACTIVATION_STATE_UNKNOWN,
        false));
  }

  return list;
}

ListValue* InternetOptionsHandler::GetRememberedList() {
  chromeos::NetworkLibrary* cros =
      chromeos::CrosLibrary::Get()->GetNetworkLibrary();
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  ListValue* list = new ListValue();

  const chromeos::WifiNetworkVector& wifi_networks =
      cros->remembered_wifi_networks();
  for (chromeos::WifiNetworkVector::const_iterator it =
      wifi_networks.begin(); it != wifi_networks.end(); ++it) {
    SkBitmap icon = *rb.GetBitmapNamed(IDR_STATUSBAR_NETWORK_BARS0_BLACK);
    if ((*it)->encrypted()) {
      icon = chromeos::NetworkMenu::IconForDisplay(icon,
          *rb.GetBitmapNamed(IDR_STATUSBAR_NETWORK_SECURE));
    }
    list->Append(GetNetwork(
        (*it)->service_path(),
        icon,
        (*it)->name(),
        (*it)->connecting(),
        (*it)->connected(),
        true,
        chromeos::TYPE_WIFI,
        true,
        chromeos::ACTIVATION_STATE_UNKNOWN,
        false));
  }
  return list;
}
