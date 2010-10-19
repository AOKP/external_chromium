// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/options/wifi_config_view.h"

#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/chromeos/cros/cros_in_process_browser_test.h"
#include "chrome/browser/chromeos/cros/mock_network_library.h"

namespace chromeos {
using ::testing::AnyNumber;
using ::testing::InvokeWithoutArgs;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::_;

class WifiConfigViewTest : public CrosInProcessBrowserTest {
 protected:
  MockNetworkLibrary *mock_network_library_;

  WifiConfigViewTest() : CrosInProcessBrowserTest() {}

  virtual void SetUpInProcessBrowserTestFixture() {
    cros_mock_->InitStatusAreaMocks();
    cros_mock_->SetStatusAreaMocksExpectations();
    mock_network_library_ = cros_mock_->mock_network_library();
  }
};

// Test that if nothing is changed, we don't call SaveWifiNetwork.
IN_PROC_BROWSER_TEST_F(WifiConfigViewTest, NoChangeSaveTest) {
  EXPECT_CALL(*mock_network_library_, SaveWifiNetwork(_)).Times(0);
  WifiConfigView* view = new WifiConfigView(NULL, WifiNetwork());
  view->Save();
}

// Test that if autoconnect was changed, we call SaveWifiNetwork.
IN_PROC_BROWSER_TEST_F(WifiConfigViewTest, ChangeAutoConnectSaveTest) {
  EXPECT_CALL(*mock_network_library_, SaveWifiNetwork(_)).Times(1);
  WifiNetwork remembered_network = WifiNetwork();
  remembered_network.set_favorite(true);
  WifiConfigView* view = new WifiConfigView(NULL, remembered_network);
  ASSERT_TRUE(view->autoconnect_checkbox_ != NULL);
  view->autoconnect_checkbox_->SetChecked(
      !view->autoconnect_checkbox_->checked());
  view->Save();
}

// Test that if password was changed, we call SaveWifiNetwork.
IN_PROC_BROWSER_TEST_F(WifiConfigViewTest, ChangePasswordSaveTest) {
  EXPECT_CALL(*mock_network_library_, SaveWifiNetwork(_)).Times(1);
  WifiNetwork wifi = WifiNetwork();
  wifi.set_encryption(SECURITY_WEP);
  WifiConfigView* view = new WifiConfigView(NULL, wifi);
  view->passphrase_textfield_->SetText(ASCIIToUTF16("test"));
  view->Save();
}

}  // namespace chromeos
