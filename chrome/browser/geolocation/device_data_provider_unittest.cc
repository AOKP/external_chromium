// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/geolocation/device_data_provider.h"

#include "chrome/browser/geolocation/wifi_data_provider_common.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {
class NullWifiDataListenerInterface
    : public WifiDataProviderCommon::ListenerInterface {
 public:
  // ListenerInterface
  virtual void DeviceDataUpdateAvailable(
      DeviceDataProvider<WifiData>* provider) {}
};
}

TEST(GeolocationDeviceDataProviderWifiData, CreateDestroy) {
  // See http://crbug.com/59913 .  The main_message_loop is not required to be
  // run for correct behaviour, but we run it in this test to help smoke out
  // any race conditions between processing in the main loop and the setup /
  // tear down of the DeviceDataProvider thread.
  MessageLoopForUI main_message_loop;
  NullWifiDataListenerInterface listener;
  for (int i = 0; i < 10; i++) {
    DeviceDataProvider<WifiData>::Register(&listener);
    for (int j = 0; j < 10; j++) {
      PlatformThread::Sleep(0);
      main_message_loop.RunAllPending();  // See comment above
    }
    DeviceDataProvider<WifiData>::Unregister(&listener);
    for (int j = 0; j < 10; j++) {
      PlatformThread::Sleep(0);
      main_message_loop.RunAllPending();  // See comment above
    }
  }
}
