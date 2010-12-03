// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/status/network_dropdown_button.h"

#include "app/l10n_util.h"
#include "app/resource_bundle.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/chromeos/cros/cros_library.h"
#include "chrome/browser/chromeos/options/network_config_view.h"
#include "chrome/browser/chromeos/status/status_area_host.h"
#include "gfx/canvas_skia.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"
#include "views/window/window.h"

namespace chromeos {

////////////////////////////////////////////////////////////////////////////////
// NetworkDropdownButton

// static
const int NetworkDropdownButton::kThrobDuration = 1000;

NetworkDropdownButton::NetworkDropdownButton(bool browser_mode,
                                             gfx::NativeWindow parent_window)
    : DropDownButton(NULL,
                     l10n_util::GetString(IDS_STATUSBAR_NO_NETWORKS_MESSAGE),
                     this,
                     true),
      browser_mode_(browser_mode),
      ALLOW_THIS_IN_INITIALIZER_LIST(animation_connecting_(this)),
      parent_window_(parent_window) {
  animation_connecting_.SetThrobDuration(kThrobDuration);
  animation_connecting_.SetTweenType(Tween::EASE_IN_OUT);
  CrosLibrary::Get()->GetNetworkLibrary()->AddNetworkManagerObserver(this);
  // The initial state will be updated on Refresh.
  // See network_selection_view.cc.
}

NetworkDropdownButton::~NetworkDropdownButton() {
  CrosLibrary::Get()->GetNetworkLibrary()->RemoveNetworkManagerObserver(this);
}

////////////////////////////////////////////////////////////////////////////////
// NetworkMenuButton, AnimationDelegate implementation:

void NetworkDropdownButton::AnimationProgressed(const Animation* animation) {
  if (animation == &animation_connecting_) {
    SetIcon(IconForNetworkConnecting(animation_connecting_.GetCurrentValue(),
                                     true));
    SchedulePaint();
  } else {
    MenuButton::AnimationProgressed(animation);
  }
}

void NetworkDropdownButton::Refresh() {
  OnNetworkManagerChanged(CrosLibrary::Get()->GetNetworkLibrary());
}

////////////////////////////////////////////////////////////////////////////////
// NetworkDropdownButton, NetworkLibrary::NetworkManagerObserver implementation:

void NetworkDropdownButton::OnNetworkManagerChanged(NetworkLibrary* cros) {
  // Show network that we will actually use. It could be another network than
  // user selected. For example user selected WiFi network but we have Ethernet
  // connection and Chrome OS device will actually use Ethernet.

  // This gets called on initialization, so any changes should be reflected
  // in CrosMock::SetNetworkLibraryStatusAreaExpectations().

  ResourceBundle& rb = ResourceBundle::GetSharedInstance();
  if (CrosLibrary::Get()->EnsureLoaded()) {
    // Always show the active network, if any
    const Network* active_network = cros->active_network();
    const WirelessNetwork* wireless;
    if (active_network != NULL) {
      animation_connecting_.Stop();
      if (active_network->type() == TYPE_ETHERNET) {
        SetIcon(*rb.GetBitmapNamed(IDR_STATUSBAR_WIRED));
        SetText(l10n_util::GetString(IDS_STATUSBAR_NETWORK_DEVICE_ETHERNET));
      } else {
        DCHECK(active_network->type() == TYPE_WIFI ||
               active_network->type() == TYPE_CELLULAR);
        wireless = static_cast<const WirelessNetwork*>(active_network);
        SetIcon(IconForNetworkStrength(wireless->strength(), true));
        SetText(ASCIIToWide(wireless->name()));
      }
    } else if (cros->wifi_connecting() || cros->cellular_connecting()) {
      if (!animation_connecting_.is_animating()) {
        animation_connecting_.Reset();
        animation_connecting_.StartThrobbing(-1);
        SetIcon(IconForNetworkConnecting(0, true));
      }
      if (cros->wifi_connecting())
        SetText(ASCIIToWide(cros->wifi_network()->name()));
      else if (cros->cellular_connecting())
        SetText(ASCIIToWide(cros->cellular_network()->name()));
    }

    if (!cros->Connected() && !cros->Connecting()) {
      animation_connecting_.Stop();
      SetIcon(SkBitmap());
      SetText(l10n_util::GetString(IDS_NETWORK_SELECTION_NONE));
    }
  } else {
    animation_connecting_.Stop();
    SetIcon(SkBitmap());
    SetText(l10n_util::GetString(IDS_STATUSBAR_NO_NETWORKS_MESSAGE));
  }

  SchedulePaint();
  UpdateMenu();
}

}  // namespace chromeos
