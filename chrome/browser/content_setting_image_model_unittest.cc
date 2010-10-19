// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/content_setting_image_model.h"

#include "chrome/browser/chrome_thread.h"
#include "chrome/browser/host_content_settings_map.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/renderer_host/test/test_render_view_host.h"
#include "chrome/browser/tab_contents/test_tab_contents.h"
#include "chrome/test/testing_profile.h"
#include "testing/gtest/include/gtest/gtest.h"

class ContentSettingImageModelTest : public RenderViewHostTestHarness {
 public:
  ContentSettingImageModelTest()
      : RenderViewHostTestHarness(),
        ui_thread_(ChromeThread::UI, &message_loop_) {}

 private:
  ChromeThread ui_thread_;

  DISALLOW_COPY_AND_ASSIGN(ContentSettingImageModelTest);
};

TEST_F(ContentSettingImageModelTest, UpdateFromTabContents) {
  TestTabContents tab_contents(profile_.get(), NULL);
  TabSpecificContentSettings* content_settings =
      tab_contents.GetTabSpecificContentSettings();
  scoped_ptr<ContentSettingImageModel> content_setting_image_model(
     ContentSettingImageModel::CreateContentSettingImageModel(
         CONTENT_SETTINGS_TYPE_IMAGES));
  EXPECT_FALSE(content_setting_image_model->is_visible());
  EXPECT_EQ(0, content_setting_image_model->get_icon());
  EXPECT_EQ(std::string(), content_setting_image_model->get_tooltip());

  content_settings->OnContentBlocked(CONTENT_SETTINGS_TYPE_IMAGES,
                                     std::string());
  content_setting_image_model->UpdateFromTabContents(&tab_contents);

  EXPECT_TRUE(content_setting_image_model->is_visible());
  EXPECT_NE(0, content_setting_image_model->get_icon());
  EXPECT_NE(std::string(), content_setting_image_model->get_tooltip());
}

TEST_F(ContentSettingImageModelTest, CookieAccessed) {
  TestTabContents tab_contents(profile_.get(), NULL);
  TabSpecificContentSettings* content_settings =
      tab_contents.GetTabSpecificContentSettings();
  profile_->GetHostContentSettingsMap()->SetDefaultContentSetting(
      CONTENT_SETTINGS_TYPE_COOKIES, CONTENT_SETTING_BLOCK);
  scoped_ptr<ContentSettingImageModel> content_setting_image_model(
     ContentSettingImageModel::CreateContentSettingImageModel(
         CONTENT_SETTINGS_TYPE_COOKIES));
  EXPECT_FALSE(content_setting_image_model->is_visible());
  EXPECT_EQ(0, content_setting_image_model->get_icon());
  EXPECT_EQ(std::string(), content_setting_image_model->get_tooltip());

  content_settings->OnCookieAccessed(GURL("http://google.com"), "A=B", false);
  content_setting_image_model->UpdateFromTabContents(&tab_contents);
  EXPECT_TRUE(content_setting_image_model->is_visible());
  EXPECT_NE(0, content_setting_image_model->get_icon());
  EXPECT_NE(std::string(), content_setting_image_model->get_tooltip());
}
