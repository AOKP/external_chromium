// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_CRASHED_EXTENSION_INFOBAR_H_
#define CHROME_BROWSER_EXTENSIONS_CRASHED_EXTENSION_INFOBAR_H_

#include <string>

#include "base/basictypes.h"
#include "chrome/browser/tab_contents/infobar_delegate.h"

class Extension;
class ExtensionsService;
class SkBitmap;

// This infobar will be displayed when an extension process crashes. It allows
// the user to reload the crashed extension.
class CrashedExtensionInfoBarDelegate : public ConfirmInfoBarDelegate {
 public:
  // |tab_contents| should point to the TabContents the infobar will be added
  // to. |extension| should be the crashed extension, and |extensions_service|
  // the ExtensionsService which manages that extension.
  CrashedExtensionInfoBarDelegate(TabContents* tab_contents,
                                  ExtensionsService* extensions_service,
                                  const Extension* extension);

  const std::string extension_id() { return extension_id_; }

  // InfoBarDelegate
  virtual CrashedExtensionInfoBarDelegate* AsCrashedExtensionInfoBarDelegate() {
    return this;
  }

  // ConfirmInfoBarDelegate
  virtual std::wstring GetMessageText() const;
  virtual void InfoBarClosed();
  virtual SkBitmap* GetIcon() const;
  virtual int GetButtons() const;
  virtual std::wstring GetButtonLabel(
      ConfirmInfoBarDelegate::InfoBarButton button) const;
  virtual bool Accept();

 private:
  ExtensionsService* extensions_service_;

  const std::string extension_id_;
  const std::string extension_name_;

  DISALLOW_COPY_AND_ASSIGN(CrashedExtensionInfoBarDelegate);
};

#endif  // CHROME_BROWSER_EXTENSIONS_CRASHED_EXTENSION_INFOBAR_H_
