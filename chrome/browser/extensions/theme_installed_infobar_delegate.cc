// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/theme_installed_infobar_delegate.h"

#include <string>

#include "app/l10n_util.h"
#include "app/resource_bundle.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/browser_theme_provider.h"
#include "chrome/browser/extensions/extensions_service.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/notification_service.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"

ThemeInstalledInfoBarDelegate::ThemeInstalledInfoBarDelegate(
    TabContents* tab_contents, const Extension* new_theme,
    const std::string& previous_theme_id)
    : ConfirmInfoBarDelegate(tab_contents),
      profile_(tab_contents->profile()),
      name_(new_theme->name()),
      theme_id_(new_theme->id()),
      previous_theme_id_(previous_theme_id),
      tab_contents_(tab_contents) {
  profile_->GetThemeProvider()->OnInfobarDisplayed();
  registrar_.Add(this, NotificationType::BROWSER_THEME_CHANGED,
                 NotificationService::AllSources());
}

ThemeInstalledInfoBarDelegate::~ThemeInstalledInfoBarDelegate() {
  // We don't want any notifications while we're running our destructor.
  registrar_.RemoveAll();

  profile_->GetThemeProvider()->OnInfobarDestroyed();
}

void ThemeInstalledInfoBarDelegate::InfoBarClosed() {
  delete this;
}

std::wstring ThemeInstalledInfoBarDelegate::GetMessageText() const {
  return l10n_util::GetStringF(IDS_THEME_INSTALL_INFOBAR_LABEL,
                               UTF8ToWide(name_));
}

SkBitmap* ThemeInstalledInfoBarDelegate::GetIcon() const {
  // TODO(aa): Reply with the theme's icon, but this requires reading it
  // asynchronously from disk.
  return ResourceBundle::GetSharedInstance().GetBitmapNamed(
      IDR_INFOBAR_THEME);
}

ThemeInstalledInfoBarDelegate*
    ThemeInstalledInfoBarDelegate::AsThemePreviewInfobarDelegate() {
  return this;
}

int ThemeInstalledInfoBarDelegate::GetButtons() const {
  return BUTTON_CANCEL;
}

std::wstring ThemeInstalledInfoBarDelegate::GetButtonLabel(
    ConfirmInfoBarDelegate::InfoBarButton button) const {
  switch (button) {
    case BUTTON_CANCEL: {
      return l10n_util::GetString(IDS_THEME_INSTALL_INFOBAR_UNDO_BUTTON);
    }
    default:
      // The InfoBar will create a default OK button and make it invisible.
      // TODO(mirandac): remove the default OK button from ConfirmInfoBar.
      return L"";
  }
}

bool ThemeInstalledInfoBarDelegate::Cancel() {
  if (!previous_theme_id_.empty()) {
    ExtensionsService* service = profile_->GetExtensionsService();
    if (service) {
      Extension* previous_theme =
          service->GetExtensionById(previous_theme_id_, true);
      if (previous_theme) {
        profile_->SetTheme(previous_theme);
        return true;
      }
    }
  }

  profile_->ClearTheme();
  return true;
}

void ThemeInstalledInfoBarDelegate::Observe(
    NotificationType type,
    const NotificationSource& source,
    const NotificationDetails& details) {
  switch (type.value) {
    case NotificationType::BROWSER_THEME_CHANGED: {
      // If the new theme is different from what this info bar is associated
      // with, close this info bar since it is no longer relevant.
      Extension* extension = Details<Extension>(details).ptr();
      if (!extension || theme_id_ != extension->id())
        tab_contents_->RemoveInfoBar(this);
      break;
    }

    default:
      NOTREACHED();
  }
}

bool ThemeInstalledInfoBarDelegate::MatchesTheme(Extension* theme) {
  return (theme && theme->id() == theme_id_);
}
