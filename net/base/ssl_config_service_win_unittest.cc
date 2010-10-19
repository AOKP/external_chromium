// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/ssl_config_service_win.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::TimeDelta;
using base::TimeTicks;

namespace {

// SSLClientConfig service caches settings for 10 seconds for performance.
// So we use synthetic time values along with the 'GetSSLConfigAt' method
// to ensure that the current settings are re-read.  By incrementing the time
// value by 11 seconds, we ensure fresh config settings.
const int kSSLConfigNextTimeInternal = 11;

class SSLConfigServiceWinObserver : public net::SSLConfigService::Observer {
 public:
  SSLConfigServiceWinObserver() : change_was_observed_(false) {
  }
  bool change_was_observed() const {
    return change_was_observed_;
  }
 protected:
  virtual void OnSSLConfigChanged() {
    change_was_observed_ = true;
  }
  bool change_was_observed_;
};

class SSLConfigServiceWinTest : public testing::Test {
};

}  // namespace

TEST(SSLConfigServiceWinTest, GetNowTest) {
  // Verify that the constructor sets the correct default values.
  net::SSLConfig config;
  EXPECT_EQ(true, config.rev_checking_enabled);
  EXPECT_EQ(false, config.ssl2_enabled);
  EXPECT_EQ(true, config.ssl3_enabled);
  EXPECT_EQ(true, config.tls1_enabled);

  bool rv = net::SSLConfigServiceWin::GetSSLConfigNow(&config);
  EXPECT_TRUE(rv);
}

TEST(SSLConfigServiceWinTest, SetTest) {
  // Save the current settings so we can restore them after the tests.
  net::SSLConfig config_save;
  bool rv = net::SSLConfigServiceWin::GetSSLConfigNow(&config_save);
  EXPECT_TRUE(rv);

  net::SSLConfig config;

  // Test SetRevCheckingEnabled.
  net::SSLConfigServiceWin::SetRevCheckingEnabled(true);
  rv = net::SSLConfigServiceWin::GetSSLConfigNow(&config);
  EXPECT_TRUE(rv);
  EXPECT_TRUE(config.rev_checking_enabled);

  net::SSLConfigServiceWin::SetRevCheckingEnabled(false);
  rv = net::SSLConfigServiceWin::GetSSLConfigNow(&config);
  EXPECT_TRUE(rv);
  EXPECT_FALSE(config.rev_checking_enabled);

  net::SSLConfigServiceWin::SetRevCheckingEnabled(
      config_save.rev_checking_enabled);

  // Test SetSSL2Enabled.
  net::SSLConfigServiceWin::SetSSL2Enabled(true);
  rv = net::SSLConfigServiceWin::GetSSLConfigNow(&config);
  EXPECT_TRUE(rv);
  EXPECT_TRUE(config.ssl2_enabled);

  net::SSLConfigServiceWin::SetSSL2Enabled(false);
  rv = net::SSLConfigServiceWin::GetSSLConfigNow(&config);
  EXPECT_TRUE(rv);
  EXPECT_FALSE(config.ssl2_enabled);

  net::SSLConfigServiceWin::SetSSL2Enabled(config_save.ssl2_enabled);
}

TEST(SSLConfigServiceWinTest, GetTest) {
  TimeTicks now = TimeTicks::Now();
  TimeTicks now_1 = now + TimeDelta::FromSeconds(1);
  TimeTicks later = now + TimeDelta::FromSeconds(kSSLConfigNextTimeInternal);

  net::SSLConfig config, config_1, config_later;
  scoped_refptr<net::SSLConfigServiceWin> config_service(
      new net::SSLConfigServiceWin(now));
  config_service->GetSSLConfigAt(&config, now);

  // Flip rev_checking_enabled.
  net::SSLConfigServiceWin::SetRevCheckingEnabled(
      !config.rev_checking_enabled);

  config_service->GetSSLConfigAt(&config_1, now_1);
  EXPECT_EQ(config.rev_checking_enabled, config_1.rev_checking_enabled);

  config_service->GetSSLConfigAt(&config_later, later);
  EXPECT_EQ(!config.rev_checking_enabled, config_later.rev_checking_enabled);

  // Restore the original value.
  net::SSLConfigServiceWin::SetRevCheckingEnabled(
      config.rev_checking_enabled);
}

TEST(SSLConfigServiceWinTest, ObserverTest) {
  TimeTicks now = TimeTicks::Now();
  TimeTicks later = now + TimeDelta::FromSeconds(kSSLConfigNextTimeInternal);

  scoped_refptr<net::SSLConfigServiceWin> config_service(
      new net::SSLConfigServiceWin(now));

  // Save the current settings so we can restore them after the tests.
  net::SSLConfig config_save;
  bool rv = net::SSLConfigServiceWin::GetSSLConfigNow(&config_save);
  EXPECT_TRUE(rv);

  net::SSLConfig config;
  net::SSLConfigServiceWin::SetSSL2Enabled(false);
  config_service->GetSSLConfigAt(&config, now);

  // Add an observer.
  SSLConfigServiceWinObserver observer;
  config_service->AddObserver(&observer);

  // Toggle SSL2.
  net::SSLConfigServiceWin::SetSSL2Enabled(!config_save.ssl2_enabled);
  config_service->GetSSLConfigAt(&config, later);

  // Verify that the observer was notified.
  EXPECT_TRUE(observer.change_was_observed());

  // Remove the observer.
  config_service->RemoveObserver(&observer);

  // Restore the original SSL2 setting.
  net::SSLConfigServiceWin::SetSSL2Enabled(config_save.ssl2_enabled);
}

