// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/status/network_menu_button.h"

#include <algorithm>
#include <limits>

#include "app/l10n_util.h"
#include "app/resource_bundle.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/options/network_config_view.h"
#include "chrome/browser/chromeos/status/status_area_host.h"
#include "gfx/canvas_skia.h"
#include "gfx/skbitmap_operations.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "views/window/window.h"

namespace chromeos {

////////////////////////////////////////////////////////////////////////////////
// NetworkMenuButton

// static
const int NetworkMenuButton::kThrobDuration = 1000;

NetworkMenuButton::NetworkMenuButton(StatusAreaHost* host)
    : StatusAreaButton(this),
      NetworkMenu(),
      host_(host),
      ALLOW_THIS_IN_INITIALIZER_LIST(animation_connecting_(this)) {
  animation_connecting_.SetThrobDuration(kThrobDuration);
  animation_connecting_.SetTweenType(Tween::EASE_IN_OUT);
  OnNetworkManagerChanged(CrosLibrary::Get()->GetNetworkLibrary());
  CrosLibrary::Get()->GetNetworkLibrary()->AddNetworkManagerObserver(this);
  CrosLibrary::Get()->GetNetworkLibrary()->AddCellularDataPlanObserver(this);
}

NetworkMenuButton::~NetworkMenuButton() {
  NetworkLibrary* netlib = CrosLibrary::Get()->GetNetworkLibrary();
  netlib->RemoveNetworkManagerObserver(this);
  netlib->RemoveObserverForAllNetworks(this);
  netlib->RemoveCellularDataPlanObserver(this);
}

////////////////////////////////////////////////////////////////////////////////
// NetworkMenuButton, AnimationDelegate implementation:

void NetworkMenuButton::AnimationProgressed(const Animation* animation) {
  if (animation == &animation_connecting_) {
    // Draw animation of bars icon fading in and out.
    // We are fading between 0 bars and a third of the opacity of 4 bars.
    // Use the current value of the animation to calculate the alpha value
    // of how transparent the icon is.
    SetIcon(SkBitmapOperations::CreateBlendedBitmap(
        *ResourceBundle::GetSharedInstance().GetBitmapNamed(
            IDR_STATUSBAR_NETWORK_BARS0),
        *ResourceBundle::GetSharedInstance().GetBitmapNamed(
            IDR_STATUSBAR_NETWORK_BARS4),
        animation_connecting_.GetCurrentValue() / 3));
    SchedulePaint();
  } else {
    MenuButton::AnimationProgressed(animation);
  }
}

////////////////////////////////////////////////////////////////////////////////
// NetworkMenuButton, StatusAreaButton implementation:

void NetworkMenuButton::DrawIcon(gfx::Canvas* canvas) {
  canvas->DrawBitmapInt(IconForDisplay(icon(), badge()),
                        horizontal_padding(), 0);
}

////////////////////////////////////////////////////////////////////////////////
// NetworkMenuButton, NetworkLibrary::NetworkManagerObserver implementation:

void NetworkMenuButton::OnNetworkManagerChanged(NetworkLibrary* cros) {
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  if (CrosLibrary::Get()->EnsureLoaded()) {
    // Add an observer for the active network, if any
    const Network* network = cros->active_network();
    if (active_network_.empty() || network == NULL ||
        active_network_ != network->service_path()) {
      if (!active_network_.empty()) {
        cros->RemoveNetworkObserver(active_network_, this);
      }
      if (network != NULL) {
        cros->AddNetworkObserver(network->service_path(), this);
      }
    }
    if (network)
      active_network_ = network->service_path();
    else
      active_network_ = "";

    if (cros->wifi_connecting() || cros->cellular_connecting()) {
      // Start the connecting animation if not running.
      if (!animation_connecting_.is_animating()) {
        animation_connecting_.Reset();
        animation_connecting_.StartThrobbing(std::numeric_limits<int>::max());
        SetIcon(*rb.GetBitmapNamed(IDR_STATUSBAR_NETWORK_BARS0));
      }
      std::string network_name = cros->wifi_connecting() ?
          cros->wifi_network()->name() : cros->cellular_network()->name();
      bool configuring = cros->wifi_connecting() ?
          cros->wifi_network()->configuring() :
          cros->cellular_network()->configuring();
      SetTooltipText(
          l10n_util::GetStringF(configuring ?
              IDS_STATUSBAR_NETWORK_CONFIGURING_TOOLTIP :
              IDS_STATUSBAR_NETWORK_CONNECTING_TOOLTIP,
              UTF8ToWide(network_name)));
    } else {
      // Stop connecting animation since we are not connecting.
      animation_connecting_.Stop();
      if (!cros->Connected()) {
        SetIcon(*rb.GetBitmapNamed(IDR_STATUSBAR_NETWORK_BARS0));
        SetTooltipText(l10n_util::GetString(
            IDS_STATUSBAR_NETWORK_NO_NETWORK_TOOLTIP));
      } else {
        SetNetworkIcon(network);
      }
    }
    SetNetworkBadge(cros, network);
  } else {
    SetIcon(*rb.GetBitmapNamed(IDR_STATUSBAR_NETWORK_BARS0));
    SetBadge(*rb.GetBitmapNamed(IDR_STATUSBAR_NETWORK_WARNING));
    SetTooltipText(l10n_util::GetString(
        IDS_STATUSBAR_NETWORK_NO_NETWORK_TOOLTIP));
  }

  SchedulePaint();
  UpdateMenu();
}

////////////////////////////////////////////////////////////////////////////////
// NetworkMenuButton, NetworkLibrary::NetworkObserver implementation:
void NetworkMenuButton::OnNetworkChanged(NetworkLibrary* cros,
                                         const Network* network) {
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  if (CrosLibrary::Get()->EnsureLoaded()) {
    // Always show the active network connection, if any.
    SetNetworkIcon(network);
    SetNetworkBadge(cros, network);
  } else {
    SetIcon(*rb.GetBitmapNamed(IDR_STATUSBAR_NETWORK_BARS0));
    SetBadge(*rb.GetBitmapNamed(IDR_STATUSBAR_NETWORK_WARNING));
    SetTooltipText(l10n_util::GetString(
        IDS_STATUSBAR_NETWORK_NO_NETWORK_TOOLTIP));
  }

  SchedulePaint();
  UpdateMenu();
}

void NetworkMenuButton::OnCellularDataPlanChanged(NetworkLibrary* cros) {
  // Call OnNetworkManagerChanged which will update the icon.
  OnNetworkManagerChanged(cros);
}

////////////////////////////////////////////////////////////////////////////////
// NetworkMenuButton, NetworkMenu implementation:

bool NetworkMenuButton::IsBrowserMode() const {
  return host_->IsBrowserMode();
}

gfx::NativeWindow NetworkMenuButton::GetNativeWindow() const {
  return host_->GetNativeWindow();
}

void NetworkMenuButton::OpenButtonOptions() {
  host_->OpenButtonOptions(this);
}

bool NetworkMenuButton::ShouldOpenButtonOptions() const {
  return host_->ShouldOpenButtonOptions(this);
}

////////////////////////////////////////////////////////////////////////////////
// NetworkMenuButton, private methods

void NetworkMenuButton::SetNetworkIcon(const Network* network) {
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  if (network && network->is_active()) {
    if (network->type() == TYPE_ETHERNET) {
      SetIcon(*rb.GetBitmapNamed(IDR_STATUSBAR_WIRED));
      SetTooltipText(
          l10n_util::GetStringF(
              IDS_STATUSBAR_NETWORK_CONNECTED_TOOLTIP,
              l10n_util::GetString(IDS_STATUSBAR_NETWORK_DEVICE_ETHERNET)));
    } else if (network->type() == TYPE_WIFI) {
      const WifiNetwork* wifi = static_cast<const WifiNetwork*>(network);
      SetIcon(IconForNetworkStrength(wifi->strength(), false));
      SetTooltipText(l10n_util::GetStringF(
          IDS_STATUSBAR_NETWORK_CONNECTED_TOOLTIP,
          UTF8ToWide(wifi->name())));
    } else if (network->type() == TYPE_CELLULAR) {
      const CellularNetwork* cellular =
          static_cast<const CellularNetwork*>(network);
      if (cellular->data_left() == CellularNetwork::DATA_NONE) {
        // If no data, then we show 0 bars.
        SetIcon(*rb.GetBitmapNamed(IDR_STATUSBAR_NETWORK_BARS0));
      } else {
        SetIcon(IconForNetworkStrength(cellular));
      }
      SetTooltipText(l10n_util::GetStringF(
          IDS_STATUSBAR_NETWORK_CONNECTED_TOOLTIP,
          UTF8ToWide(cellular->name())));
    }
  }
}

void NetworkMenuButton::SetNetworkBadge(NetworkLibrary* cros,
                                        const Network* network) {
  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  // Figure out whether or not to show a badge.
  if (network && network->type() == TYPE_CELLULAR &&
      (network->is_active() || network->connecting())) {
    const CellularNetwork* cellular
        = static_cast<const CellularNetwork*>(network);
    SetBadge(BadgeForNetworkTechnology(cellular));
  } else if (!cros->Connected() && !cros->Connecting()) {
    SetBadge(*rb.GetBitmapNamed(IDR_STATUSBAR_NETWORK_DISCONNECTED));
  } else {
    SetBadge(SkBitmap());
  }
}

}  // namespace chromeos
