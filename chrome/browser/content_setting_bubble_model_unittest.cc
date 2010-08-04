// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/content_setting_bubble_model.h"

#include "chrome/browser/profile.h"
#include "chrome/browser/renderer_host/test/test_render_view_host.h"
#include "chrome/browser/tab_contents/test_tab_contents.h"
#include "chrome/test/testing_profile.h"
#include "testing/gtest/include/gtest/gtest.h"

class ContentSettingBubbleModelTest : public RenderViewHostTestHarness {
 protected:
  ContentSettingBubbleModelTest()
      : ui_thread_(ChromeThread::UI, MessageLoop::current()) {
  }

  void CheckGeolocationBubble(size_t expected_domains,
                              bool expect_clear_link,
                              bool expect_reload_hint) {
    scoped_ptr<ContentSettingBubbleModel> content_setting_bubble_model(
        ContentSettingBubbleModel::CreateContentSettingBubbleModel(
           contents(), profile_.get(), CONTENT_SETTINGS_TYPE_GEOLOCATION));
    const ContentSettingBubbleModel::BubbleContent& bubble_content =
        content_setting_bubble_model->bubble_content();
    EXPECT_EQ(0U, bubble_content.radio_group.radio_items.size());
    EXPECT_EQ(0U, bubble_content.popup_items.size());
    // The reload hint is currently implemented as a tacked on domain title, so
    // account for this.
    if (expect_reload_hint)
      ++expected_domains;
    EXPECT_EQ(expected_domains, bubble_content.domain_lists.size());
    if (expect_clear_link)
      EXPECT_NE(std::string(), bubble_content.clear_link);
    else
      EXPECT_EQ(std::string(), bubble_content.clear_link);
    EXPECT_NE(std::string(), bubble_content.manage_link);
    EXPECT_EQ(std::string(), bubble_content.info_link);
    EXPECT_EQ(std::string(), bubble_content.title);
  }

  ChromeThread ui_thread_;
};

TEST_F(ContentSettingBubbleModelTest, ImageRadios) {
  TabSpecificContentSettings* content_settings =
      contents()->GetTabSpecificContentSettings();
  content_settings->OnContentBlocked(CONTENT_SETTINGS_TYPE_IMAGES);

  scoped_ptr<ContentSettingBubbleModel> content_setting_bubble_model(
      ContentSettingBubbleModel::CreateContentSettingBubbleModel(
         contents(), profile_.get(), CONTENT_SETTINGS_TYPE_IMAGES));
  const ContentSettingBubbleModel::BubbleContent& bubble_content =
      content_setting_bubble_model->bubble_content();
  EXPECT_EQ(2U, bubble_content.radio_group.radio_items.size());
  EXPECT_EQ(0, bubble_content.radio_group.default_item);
  EXPECT_NE(std::string(), bubble_content.manage_link);
  EXPECT_EQ(std::string(), bubble_content.info_link);
  EXPECT_NE(std::string(), bubble_content.title);
}

TEST_F(ContentSettingBubbleModelTest, Cookies) {
  TabSpecificContentSettings* content_settings =
      contents()->GetTabSpecificContentSettings();
  content_settings->OnContentBlocked(CONTENT_SETTINGS_TYPE_COOKIES);

  scoped_ptr<ContentSettingBubbleModel> content_setting_bubble_model(
      ContentSettingBubbleModel::CreateContentSettingBubbleModel(
         contents(), profile_.get(), CONTENT_SETTINGS_TYPE_COOKIES));
  const ContentSettingBubbleModel::BubbleContent& bubble_content =
      content_setting_bubble_model->bubble_content();
  EXPECT_EQ(0U, bubble_content.radio_group.radio_items.size());
  EXPECT_NE(std::string(), bubble_content.manage_link);
  EXPECT_NE(std::string(), bubble_content.info_link);
  EXPECT_NE(std::string(), bubble_content.title);
}

TEST_F(ContentSettingBubbleModelTest, Geolocation) {
  const GURL page_url("http://toplevel.example/");
  const GURL frame1_url("http://host1.example/");
  const GURL frame2_url("http://host2.example:999/");

  NavigateAndCommit(page_url);
  TabSpecificContentSettings* content_settings =
      contents()->GetTabSpecificContentSettings();

  // One permitted frame, but not in the content map: requires reload.
  content_settings->OnGeolocationPermissionSet(frame1_url, true);
  CheckGeolocationBubble(1, false, true);

  // Add it to the content map, should now have a clear link.
  GeolocationContentSettingsMap* setting_map =
      profile_->GetGeolocationContentSettingsMap();
  setting_map->SetContentSetting(frame1_url, page_url, CONTENT_SETTING_ALLOW);
  CheckGeolocationBubble(1, true, false);

  // Change the default to allow: no message needed.
  setting_map->SetDefaultContentSetting(CONTENT_SETTING_ALLOW);
  CheckGeolocationBubble(1, false, false);

  // Second frame denied, but not stored in the content map: requires reload.
  content_settings->OnGeolocationPermissionSet(frame2_url, false);
  CheckGeolocationBubble(2, false, true);

  // Change the default to block: offer a clear link for the persisted frame 1.
  setting_map->SetDefaultContentSetting(CONTENT_SETTING_BLOCK);
  CheckGeolocationBubble(2, true, false);
}
