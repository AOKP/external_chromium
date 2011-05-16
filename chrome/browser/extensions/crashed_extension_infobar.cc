// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/crashed_extension_infobar.h"

#include "app/l10n_util.h"
#include "app/resource_bundle.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/common/extensions/extension.h"
#include "grit/browser_resources.h"
#include "grit/generated_resources.h"
#include "grit/theme_resources.h"

CrashedExtensionInfoBarDelegate::CrashedExtensionInfoBarDelegate(
    TabContents* tab_contents,
    ExtensionService* extensions_service,
    const Extension* extension)
    : ConfirmInfoBarDelegate(tab_contents),
      extensions_service_(extensions_service),
      extension_id_(extension->id()),
      extension_name_(extension->name()) {
  DCHECK(extensions_service_);
  DCHECK(!extension_id_.empty());
}

CrashedExtensionInfoBarDelegate* CrashedExtensionInfoBarDelegate::
AsCrashedExtensionInfoBarDelegate() {
  return this;
}

bool CrashedExtensionInfoBarDelegate::ShouldExpire(
    const NavigationController::LoadCommittedDetails& details) const {
  return false;
}

string16 CrashedExtensionInfoBarDelegate::GetMessageText() const {
  return l10n_util::GetStringFUTF16(IDS_EXTENSION_CRASHED_INFOBAR_MESSAGE,
      UTF8ToUTF16(extension_name_));
}

void CrashedExtensionInfoBarDelegate::InfoBarClosed() {
  delete this;
}

SkBitmap* CrashedExtensionInfoBarDelegate::GetIcon() const {
  // TODO(erikkay): Create extension-specific icon. http://crbug.com/14591
  return ResourceBundle::GetSharedInstance().GetBitmapNamed(
      IDR_INFOBAR_PLUGIN_CRASHED);
}

int CrashedExtensionInfoBarDelegate::GetButtons() const {
  return BUTTON_OK;
}

string16 CrashedExtensionInfoBarDelegate::GetButtonLabel(
    ConfirmInfoBarDelegate::InfoBarButton button) const {
  if (button == BUTTON_OK) {
    return l10n_util::GetStringUTF16(
        IDS_EXTENSION_CRASHED_INFOBAR_RESTART_BUTTON);
  }
  return ConfirmInfoBarDelegate::GetButtonLabel(button);
}

bool CrashedExtensionInfoBarDelegate::Accept() {
  extensions_service_->ReloadExtension(extension_id_);
  return true;
}
