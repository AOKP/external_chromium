// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/dom_ui/content_settings_handler.h"

#include "app/l10n_util.h"
#include "base/callback.h"
#include "base/command_line.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/geolocation/geolocation_content_settings_map.h"
#include "chrome/browser/host_content_settings_map.h"
#include "chrome/browser/notifications/desktop_notification_service.h"
#include "chrome/browser/profile.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/notification_source.h"
#include "chrome/common/notification_type.h"
#include "chrome/common/url_constants.h"
#include "grit/generated_resources.h"
#include "grit/locale_settings.h"

typedef HostContentSettingsMap::ContentSettingsDetails ContentSettingsDetails;

namespace {

ContentSettingsType ContentSettingsTypeFromGroupName(const std::string& name) {
  if (name == "cookies")
    return CONTENT_SETTINGS_TYPE_COOKIES;
  if (name == "images")
    return CONTENT_SETTINGS_TYPE_IMAGES;
  if (name == "javascript")
    return CONTENT_SETTINGS_TYPE_JAVASCRIPT;
  if (name == "plugins")
    return CONTENT_SETTINGS_TYPE_PLUGINS;
  if (name == "popups")
    return CONTENT_SETTINGS_TYPE_POPUPS;
  if (name == "location")
    return CONTENT_SETTINGS_TYPE_GEOLOCATION;
  if (name == "notifications")
    return CONTENT_SETTINGS_TYPE_NOTIFICATIONS;

  NOTREACHED();
  return CONTENT_SETTINGS_TYPE_DEFAULT;
}

std::string ContentSettingToString(ContentSetting setting) {
  switch (setting) {
    case CONTENT_SETTING_ALLOW:
      return "allow";
    case CONTENT_SETTING_ASK:
      return "ask";
    case CONTENT_SETTING_BLOCK:
      return "block";
    case CONTENT_SETTING_SESSION_ONLY:
      return "session";

    default:
      NOTREACHED();
      return "";
  }
}

ContentSetting ContentSettingFromString(const std::string& name) {
  if (name == "allow")
    return CONTENT_SETTING_ALLOW;
  if (name == "ask")
    return CONTENT_SETTING_ASK;
  if (name == "block")
    return CONTENT_SETTING_BLOCK;
  if (name == "session")
    return CONTENT_SETTING_SESSION_ONLY;

  NOTREACHED();
  return CONTENT_SETTING_DEFAULT;
}

}  // namespace

ContentSettingsHandler::ContentSettingsHandler() {
}

ContentSettingsHandler::~ContentSettingsHandler() {
}

void ContentSettingsHandler::GetLocalizedValues(
    DictionaryValue* localized_strings) {
  DCHECK(localized_strings);

  localized_strings->SetString("content_exceptions",
      l10n_util::GetStringUTF16(IDS_COOKIES_EXCEPTIONS_BUTTON));
  localized_strings->SetString("contentSettingsPage",
      l10n_util::GetStringUTF16(IDS_CONTENT_SETTINGS_TITLE));
  localized_strings->SetString("allowException",
      l10n_util::GetStringUTF16(IDS_EXCEPTIONS_ALLOW_BUTTON));
  localized_strings->SetString("blockException",
      l10n_util::GetStringUTF16(IDS_EXCEPTIONS_BLOCK_BUTTON));
  localized_strings->SetString("sessionException",
      l10n_util::GetStringUTF16(IDS_EXCEPTIONS_SESSION_ONLY_BUTTON));
  localized_strings->SetString("askException",
      l10n_util::GetStringUTF16(IDS_EXCEPTIONS_ASK_BUTTON));
  localized_strings->SetString("addExceptionRow",
      l10n_util::GetStringUTF16(IDS_EXCEPTIONS_ADD_BUTTON));
  localized_strings->SetString("removeExceptionRow",
      l10n_util::GetStringUTF16(IDS_EXCEPTIONS_REMOVE_BUTTON));
  localized_strings->SetString("editExceptionRow",
      l10n_util::GetStringUTF16(IDS_EXCEPTIONS_EDIT_BUTTON));
  localized_strings->SetString("otr_exceptions_explanation",
      l10n_util::GetStringUTF16(IDS_EXCEPTIONS_OTR_LABEL));

  // Cookies filter.
  localized_strings->SetString("cookies_tab_label",
      l10n_util::GetStringUTF16(IDS_COOKIES_TAB_LABEL));
  localized_strings->SetString("cookies_modify",
      l10n_util::GetStringUTF16(IDS_MODIFY_COOKIE_STORING_LABEL));
  localized_strings->SetString("cookies_allow",
      l10n_util::GetStringUTF16(IDS_COOKIES_ALLOW_RADIO));
  localized_strings->SetString("cookies_ask",
      l10n_util::GetStringUTF16(IDS_COOKIES_ASK_EVERY_TIME_RADIO));
  localized_strings->SetString("cookies_block",
      l10n_util::GetStringUTF16(IDS_COOKIES_BLOCK_RADIO));
  localized_strings->SetString("cookies_block_3rd_party",
      l10n_util::GetStringUTF16(IDS_COOKIES_BLOCK_3RDPARTY_CHKBOX));
  localized_strings->SetString("cookies_clear_on_exit",
      l10n_util::GetStringUTF16(IDS_COOKIES_CLEAR_WHEN_CLOSE_CHKBOX));
  localized_strings->SetString("cookies_show_cookies",
      l10n_util::GetStringUTF16(IDS_COOKIES_SHOW_COOKIES_BUTTON));
  localized_strings->SetString("flash_storage_settings",
      l10n_util::GetStringUTF16(IDS_FLASH_STORAGE_SETTINGS));
  localized_strings->SetString("flash_storage_url",
      l10n_util::GetStringUTF16(IDS_FLASH_STORAGE_URL));

  // Image filter.
  localized_strings->SetString("images_tab_label",
      l10n_util::GetStringUTF16(IDS_IMAGES_TAB_LABEL));
  localized_strings->SetString("images_setting",
      l10n_util::GetStringUTF16(IDS_IMAGES_SETTING_LABEL));
  localized_strings->SetString("images_allow",
      l10n_util::GetStringUTF16(IDS_IMAGES_LOAD_RADIO));
  localized_strings->SetString("images_block",
      l10n_util::GetStringUTF16(IDS_IMAGES_NOLOAD_RADIO));

  // JavaScript filter.
  localized_strings->SetString("javascript_tab_label",
      l10n_util::GetStringUTF16(IDS_JAVASCRIPT_TAB_LABEL));
  localized_strings->SetString("javascript_setting",
      l10n_util::GetStringUTF16(IDS_JS_SETTING_LABEL));
  localized_strings->SetString("javascript_allow",
      l10n_util::GetStringUTF16(IDS_JS_ALLOW_RADIO));
  localized_strings->SetString("javascript_block",
      l10n_util::GetStringUTF16(IDS_JS_DONOTALLOW_RADIO));

  // Plug-ins filter.
  localized_strings->SetString("plugins_tab_label",
      l10n_util::GetStringUTF16(IDS_PLUGIN_TAB_LABEL));
  localized_strings->SetString("plugins_setting",
      l10n_util::GetStringUTF16(IDS_PLUGIN_SETTING_LABEL));
  localized_strings->SetString("plugins_allow_sandboxed",
      l10n_util::GetStringUTF16(IDS_PLUGIN_LOAD_SANDBOXED_RADIO));
  localized_strings->SetString("plugins_allow",
      l10n_util::GetStringUTF16(IDS_PLUGIN_LOAD_RADIO));
  localized_strings->SetString("plugins_block",
      l10n_util::GetStringUTF16(IDS_PLUGIN_NOLOAD_RADIO));
  localized_strings->SetString("disable_individual_plugins",
      l10n_util::GetStringUTF16(IDS_PLUGIN_SELECTIVE_DISABLE));
  localized_strings->SetString("chrome_plugin_url",
      chrome::kChromeUIPluginsURL);

  // Pop-ups filter.
  localized_strings->SetString("popups_tab_label",
      l10n_util::GetStringUTF16(IDS_POPUP_TAB_LABEL));
  localized_strings->SetString("popups_setting",
      l10n_util::GetStringUTF16(IDS_POPUP_SETTING_LABEL));
  localized_strings->SetString("popups_allow",
      l10n_util::GetStringUTF16(IDS_POPUP_ALLOW_RADIO));
  localized_strings->SetString("popups_block",
      l10n_util::GetStringUTF16(IDS_POPUP_BLOCK_RADIO));

  // Location filter.
  localized_strings->SetString("location_tab_label",
      l10n_util::GetStringUTF16(IDS_GEOLOCATION_TAB_LABEL));
  localized_strings->SetString("location_setting",
      l10n_util::GetStringUTF16(IDS_GEOLOCATION_SETTING_LABEL));
  localized_strings->SetString("location_allow",
      l10n_util::GetStringUTF16(IDS_GEOLOCATION_ALLOW_RADIO));
  localized_strings->SetString("location_ask",
      l10n_util::GetStringUTF16(IDS_GEOLOCATION_ASK_RADIO));
  localized_strings->SetString("location_block",
      l10n_util::GetStringUTF16(IDS_GEOLOCATION_BLOCK_RADIO));

  // Notifications filter.
  localized_strings->SetString("notifications_tab_label",
      l10n_util::GetStringUTF16(IDS_NOTIFICATIONS_TAB_LABEL));
  localized_strings->SetString("notifications_setting",
      l10n_util::GetStringUTF16(IDS_NOTIFICATIONS_SETTING_LABEL));
  localized_strings->SetString("notifications_allow",
      l10n_util::GetStringUTF16(IDS_NOTIFICATIONS_ALLOW_RADIO));
  localized_strings->SetString("notifications_ask",
      l10n_util::GetStringUTF16(IDS_NOTIFICATIONS_ASK_RADIO));
  localized_strings->SetString("notifications_block",
      l10n_util::GetStringUTF16(IDS_NOTIFICATIONS_BLOCK_RADIO));
}

void ContentSettingsHandler::Initialize() {
  const HostContentSettingsMap* settings_map = GetContentSettingsMap();
  scoped_ptr<Value> block_3rd_party(Value::CreateBooleanValue(
      settings_map->BlockThirdPartyCookies()));
  dom_ui_->CallJavascriptFunction(
      L"ContentSettings.setBlockThirdPartyCookies", *block_3rd_party.get());

  UpdateAllExceptionsViewsFromModel();
  notification_registrar_.Add(
      this, NotificationType::CONTENT_SETTINGS_CHANGED,
      Source<const HostContentSettingsMap>(settings_map));
}

void ContentSettingsHandler::Observe(NotificationType type,
                                     const NotificationSource& source,
                                     const NotificationDetails& details) {
  if (type != NotificationType::CONTENT_SETTINGS_CHANGED)
    return OptionsPageUIHandler::Observe(type, source, details);

  const ContentSettingsDetails* settings_details =
      static_cast<Details<const ContentSettingsDetails> >(details).ptr();

  // TODO(estade): we pretend update_all() is always true.
  if (settings_details->update_all_types())
    UpdateAllExceptionsViewsFromModel();
  else
    UpdateExceptionsViewFromModel(settings_details->type());
}

void ContentSettingsHandler::UpdateExceptionsDefaultFromModel(
    ContentSettingsType type) {
  DictionaryValue filter_settings;
  filter_settings.SetString(ContentSettingsTypeToGroupName(type),
                            GetExceptionsDefaultFromModel(type));

  dom_ui_->CallJavascriptFunction(
      L"ContentSettings.setContentFilterSettingsValue", filter_settings);
}

std::string ContentSettingsHandler::GetExceptionsDefaultFromModel(
    ContentSettingsType type) {
  ContentSetting default_setting;
  const HostContentSettingsMap* settings_map = GetContentSettingsMap();
  if (type == CONTENT_SETTINGS_TYPE_PLUGINS) {
    default_setting = settings_map->GetDefaultContentSetting(type);
    if (settings_map->GetBlockNonsandboxedPlugins())
      default_setting = CONTENT_SETTING_ASK;
  } else if (type == CONTENT_SETTINGS_TYPE_GEOLOCATION) {
    default_setting = dom_ui_->GetProfile()->
        GetGeolocationContentSettingsMap()->GetDefaultContentSetting();
  } else if (type == CONTENT_SETTINGS_TYPE_NOTIFICATIONS) {
    default_setting = dom_ui_->GetProfile()->
        GetDesktopNotificationService()->GetDefaultContentSetting();
  } else {
    default_setting = GetContentSettingsMap()->GetDefaultContentSetting(type);
  }

  return ContentSettingToString(default_setting);
}

void ContentSettingsHandler::UpdateAllExceptionsViewsFromModel() {
  for (int type = CONTENT_SETTINGS_TYPE_DEFAULT + 1;
       type < CONTENT_SETTINGS_NUM_TYPES; ++type) {
    UpdateExceptionsViewFromModel(static_cast<ContentSettingsType>(type));
  }
}

void ContentSettingsHandler::UpdateExceptionsViewFromModel(
    ContentSettingsType type) {
  HostContentSettingsMap::SettingsForOneType entries;
  GetContentSettingsMap()->GetSettingsForOneType(type, "", &entries);

  ListValue exceptions;
  for (size_t i = 0; i < entries.size(); ++i) {
    ListValue* exception = new ListValue();
    exception->Append(new StringValue(entries[i].first.AsString()));
    exception->Append(
        new StringValue(ContentSettingToString(entries[i].second)));
    exceptions.Append(exception);
  }

  StringValue type_string(ContentSettingsTypeToGroupName(type));
  dom_ui_->CallJavascriptFunction(
      L"ContentSettings.setExceptions", type_string, exceptions);

  // The default may also have changed (we won't get a separate notification).
  // If it hasn't changed, this call will be harmless.
  UpdateExceptionsDefaultFromModel(type);

  const HostContentSettingsMap* otr_settings_map = GetOTRContentSettingsMap();
  if (otr_settings_map && type != CONTENT_SETTINGS_TYPE_GEOLOCATION &&
                          type != CONTENT_SETTINGS_TYPE_NOTIFICATIONS) {
    HostContentSettingsMap::SettingsForOneType otr_entries;
    otr_settings_map->GetSettingsForOneType(type, "", &otr_entries);

    ListValue otr_exceptions;
    for (size_t i = 0; i < otr_entries.size(); ++i) {
      ListValue* exception = new ListValue();
      exception->Append(new StringValue(otr_entries[i].first.AsString()));
      exception->Append(
          new StringValue(ContentSettingToString(otr_entries[i].second)));
      otr_exceptions.Append(exception);
    }

    dom_ui_->CallJavascriptFunction(
        L"ContentSettings.setOTRExceptions", type_string, otr_exceptions);
  }
}

void ContentSettingsHandler::RegisterMessages() {
  dom_ui_->RegisterMessageCallback("setContentFilter",
      NewCallback(this,
                  &ContentSettingsHandler::SetContentFilter));
  dom_ui_->RegisterMessageCallback("setAllowThirdPartyCookies",
      NewCallback(this,
                  &ContentSettingsHandler::SetAllowThirdPartyCookies));
  dom_ui_->RegisterMessageCallback("removeExceptions",
      NewCallback(this,
                  &ContentSettingsHandler::RemoveExceptions));
  dom_ui_->RegisterMessageCallback("setException",
      NewCallback(this,
                  &ContentSettingsHandler::SetException));
  dom_ui_->RegisterMessageCallback("checkExceptionPatternValidity",
      NewCallback(this,
                  &ContentSettingsHandler::CheckExceptionPatternValidity));
}

void ContentSettingsHandler::SetContentFilter(const ListValue* args) {
  DCHECK_EQ(2U, args->GetSize());
  std::string group, setting;
  if (!(args->GetString(0, &group) &&
        args->GetString(1, &setting))) {
    NOTREACHED();
    return;
  }

  ContentSetting default_setting = ContentSettingFromString(setting);
  ContentSettingsType content_type = ContentSettingsTypeFromGroupName(group);
  if (content_type == CONTENT_SETTINGS_TYPE_PLUGINS) {
    if (default_setting == CONTENT_SETTING_ASK) {
      default_setting = CONTENT_SETTING_ALLOW;
      GetContentSettingsMap()->SetBlockNonsandboxedPlugins(true);
    } else {
      GetContentSettingsMap()->SetBlockNonsandboxedPlugins(false);
    }
    GetContentSettingsMap()->
        SetDefaultContentSetting(content_type, default_setting);
  } else if (content_type == CONTENT_SETTINGS_TYPE_GEOLOCATION) {
    dom_ui_->GetProfile()->GetGeolocationContentSettingsMap()->
        SetDefaultContentSetting(default_setting);
  } else if (content_type == CONTENT_SETTINGS_TYPE_NOTIFICATIONS) {
    dom_ui_->GetProfile()->GetDesktopNotificationService()->
        SetDefaultContentSetting(default_setting);
  } else {
    GetContentSettingsMap()->
        SetDefaultContentSetting(content_type, default_setting);
  }
}

void ContentSettingsHandler::SetAllowThirdPartyCookies(const ListValue* args) {
  std::wstring allow = ExtractStringValue(args);

  GetContentSettingsMap()->SetBlockThirdPartyCookies(allow == L"true");
}

void ContentSettingsHandler::RemoveExceptions(const ListValue* args) {
  size_t arg_i = 0;
  std::string type_string;
  CHECK(args->GetString(arg_i++, &type_string));

  ContentSettingsType type = ContentSettingsTypeFromGroupName(type_string);
  while (arg_i < args->GetSize()) {
    if (type == CONTENT_SETTINGS_TYPE_GEOLOCATION) {
      std::string origin;
      std::string embedding_origin;
      bool rv = args->GetString(arg_i++, &origin);
      DCHECK(rv);
      rv = args->GetString(arg_i++, &embedding_origin);
      DCHECK(rv);

      dom_ui_->GetProfile()->GetGeolocationContentSettingsMap()->
          SetContentSetting(GURL(origin),
                            GURL(embedding_origin),
                            CONTENT_SETTING_DEFAULT);
    } else if (type == CONTENT_SETTINGS_TYPE_NOTIFICATIONS) {
      std::string origin;
      std::string setting;
      bool rv = args->GetString(arg_i++, &origin);
      DCHECK(rv);
      rv = args->GetString(arg_i++, &setting);
      DCHECK(rv);
      ContentSetting content_setting = ContentSettingFromString(setting);
      if (content_setting == CONTENT_SETTING_ALLOW) {
        dom_ui_->GetProfile()->GetDesktopNotificationService()->
            ResetAllowedOrigin(GURL(origin));
      } else {
        DCHECK_EQ(content_setting, CONTENT_SETTING_BLOCK);
        dom_ui_->GetProfile()->GetDesktopNotificationService()->
            ResetBlockedOrigin(GURL(origin));
      }
    } else {
      std::string mode;
      bool rv = args->GetString(arg_i++, &mode);
      DCHECK(rv);

      std::string pattern;
      rv = args->GetString(arg_i++, &pattern);
      DCHECK(rv);

      HostContentSettingsMap* settings_map =
          mode == "normal" ? GetContentSettingsMap() :
                             GetOTRContentSettingsMap();
      // The settings map could be null if the mode was OTR but the OTR profile
      // got destroyed before we received this message.
      if (settings_map) {
        settings_map->SetContentSetting(
            HostContentSettingsMap::Pattern(pattern),
            ContentSettingsTypeFromGroupName(type_string),
            "",
            CONTENT_SETTING_DEFAULT);
      }
    }
  }
}

void ContentSettingsHandler::SetException(const ListValue* args) {
  size_t arg_i = 0;
  std::string type_string;
  CHECK(args->GetString(arg_i++, &type_string));
  std::string mode;
  CHECK(args->GetString(arg_i++, &mode));
  std::string pattern;
  CHECK(args->GetString(arg_i++, &pattern));
  std::string setting;
  CHECK(args->GetString(arg_i++, &setting));

  ContentSettingsType type = ContentSettingsTypeFromGroupName(type_string);
  if (type == CONTENT_SETTINGS_TYPE_GEOLOCATION ||
      type == CONTENT_SETTINGS_TYPE_NOTIFICATIONS) {
    NOTREACHED();
    return;
  }

  HostContentSettingsMap* settings_map =
      mode == "normal" ? GetContentSettingsMap() :
                         GetOTRContentSettingsMap();

  // The settings map could be null if the mode was OTR but the OTR profile
  // got destroyed before we received this message.
  if (!settings_map)
    return;

  settings_map->
      SetContentSetting(HostContentSettingsMap::Pattern(pattern),
                        type,
                        "",
                        ContentSettingFromString(setting));
}

void ContentSettingsHandler::CheckExceptionPatternValidity(
    const ListValue* args) {
  size_t arg_i = 0;
  Value* type;
  CHECK(args->Get(arg_i++, &type));
  std::string mode_string;
  CHECK(args->GetString(arg_i++, &mode_string));
  std::string pattern_string;
  CHECK(args->GetString(arg_i++, &pattern_string));

  HostContentSettingsMap::Pattern pattern(pattern_string);

  scoped_ptr<Value> mode_value(Value::CreateStringValue(mode_string));
  scoped_ptr<Value> pattern_value(Value::CreateStringValue(pattern_string));
  scoped_ptr<Value> valid_value(Value::CreateBooleanValue(pattern.IsValid()));

  dom_ui_->CallJavascriptFunction(
      L"ContentSettings.patternValidityCheckComplete", *type,
                                                       *mode_value.get(),
                                                       *pattern_value.get(),
                                                       *valid_value.get());
}

// static
std::string ContentSettingsHandler::ContentSettingsTypeToGroupName(
    ContentSettingsType type) {
  switch (type) {
    case CONTENT_SETTINGS_TYPE_COOKIES:
      return "cookies";
    case CONTENT_SETTINGS_TYPE_IMAGES:
      return "images";
    case CONTENT_SETTINGS_TYPE_JAVASCRIPT:
      return "javascript";
    case CONTENT_SETTINGS_TYPE_PLUGINS:
      return "plugins";
    case CONTENT_SETTINGS_TYPE_POPUPS:
      return "popups";
    case CONTENT_SETTINGS_TYPE_GEOLOCATION:
      return "location";
    case CONTENT_SETTINGS_TYPE_NOTIFICATIONS:
      return "notifications";

    default:
      NOTREACHED();
      return "";
  }
}

HostContentSettingsMap* ContentSettingsHandler::GetContentSettingsMap() {
  return dom_ui_->GetProfile()->GetHostContentSettingsMap();
}

HostContentSettingsMap*
    ContentSettingsHandler::GetOTRContentSettingsMap() {
  Profile* profile = dom_ui_->GetProfile();
  if (profile->HasOffTheRecordProfile())
    return profile->GetOffTheRecordProfile()->GetHostContentSettingsMap();
  return NULL;
}
