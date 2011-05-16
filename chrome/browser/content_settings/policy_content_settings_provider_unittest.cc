// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/content_settings/policy_content_settings_provider.h"

#include "chrome/browser/content_settings/host_content_settings_map_unittest.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/testing_pref_service.h"
#include "chrome/test/testing_profile.h"
#include "testing/gtest/include/gtest/gtest.h"


namespace {

class PolicyContentSettingsProviderTest : public testing::Test {
 public:
  PolicyContentSettingsProviderTest()
      : ui_thread_(BrowserThread::UI, &message_loop_) {
  }

 protected:
  MessageLoop message_loop_;
  BrowserThread ui_thread_;
};

TEST_F(PolicyContentSettingsProviderTest, DefaultValues) {
  TestingProfile profile;
  PolicyContentSettingsProvider provider(&profile);
  TestingPrefService* prefs = profile.GetTestingPrefService();

  // By default, policies should be off.
  ASSERT_FALSE(
      provider.CanProvideDefaultSetting(CONTENT_SETTINGS_TYPE_COOKIES));
  ASSERT_FALSE(
      provider.DefaultSettingIsManaged(CONTENT_SETTINGS_TYPE_COOKIES));

  // Set managed-default-content-setting through the coresponding preferences.
  prefs->SetManagedPref(prefs::kManagedDefaultCookiesSetting,
                        Value::CreateIntegerValue(CONTENT_SETTING_BLOCK));
  ASSERT_TRUE(
      provider.CanProvideDefaultSetting(CONTENT_SETTINGS_TYPE_COOKIES));
  ASSERT_TRUE(
      provider.DefaultSettingIsManaged(CONTENT_SETTINGS_TYPE_COOKIES));
  ASSERT_EQ(CONTENT_SETTING_BLOCK,
            provider.ProvideDefaultSetting(CONTENT_SETTINGS_TYPE_COOKIES));

  // Remove managed-default-content-settings-preferences.
  prefs->RemoveManagedPref(prefs::kManagedDefaultCookiesSetting);
  ASSERT_FALSE(
      provider.CanProvideDefaultSetting(CONTENT_SETTINGS_TYPE_COOKIES));
  ASSERT_FALSE(
      provider.DefaultSettingIsManaged(CONTENT_SETTINGS_TYPE_COOKIES));
}

// When a default-content-setting is set to a managed setting a
// CONTENT_SETTINGS_CHANGED notification should be fired. The same should happen
// if the managed setting is removed.
TEST_F(PolicyContentSettingsProviderTest, ObserveManagedSettingsChange) {
  TestingProfile profile;
  StubSettingsObserver observer;
  // Make sure the content settings map exists.
  profile.GetHostContentSettingsMap();
  TestingPrefService* prefs = profile.GetTestingPrefService();

  // Set the managed default-content-setting.
  prefs->SetManagedPref(prefs::kManagedDefaultImagesSetting,
                        Value::CreateIntegerValue(CONTENT_SETTING_BLOCK));
  EXPECT_EQ(profile.GetHostContentSettingsMap(), observer.last_notifier);
  EXPECT_EQ(ContentSettingsPattern(), observer.last_pattern);
  EXPECT_EQ(CONTENT_SETTINGS_TYPE_DEFAULT, observer.last_type);
  EXPECT_TRUE(observer.last_update_all);
  EXPECT_TRUE(observer.last_update_all_types);
  EXPECT_EQ(1, observer.counter);

  // Remove the managed default-content-setting.
  prefs->RemoveManagedPref(prefs::kManagedDefaultImagesSetting);
  EXPECT_EQ(profile.GetHostContentSettingsMap(), observer.last_notifier);
  EXPECT_EQ(CONTENT_SETTINGS_TYPE_DEFAULT, observer.last_type);
  EXPECT_EQ(ContentSettingsPattern(), observer.last_pattern);
  EXPECT_TRUE(observer.last_update_all);
  EXPECT_TRUE(observer.last_update_all_types);
  EXPECT_EQ(2, observer.counter);
}

}  // namespace
