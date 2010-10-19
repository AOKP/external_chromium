// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_AUTOFILL_AUTOFILL_CC_INFOBAR_DELEGATE_H_
#define CHROME_BROWSER_AUTOFILL_AUTOFILL_CC_INFOBAR_DELEGATE_H_
#pragma once

#include "base/string16.h"
#include "chrome/browser/tab_contents/infobar_delegate.h"

class AutoFillManager;
class SkBitmap;
class TabContents;

// An InfoBar delegate that enables the user to allow or deny storing credit
// card information gathered from a form submission.
class AutoFillCCInfoBarDelegate : public ConfirmInfoBarDelegate {
 public:
  AutoFillCCInfoBarDelegate(TabContents* tab_contents, AutoFillManager* host);
  virtual ~AutoFillCCInfoBarDelegate();

  // ConfirmInfoBarDelegate implementation.
  virtual bool ShouldExpire(
     const NavigationController::LoadCommittedDetails& details) const;
  virtual void InfoBarClosed();
  virtual string16 GetMessageText() const;
  virtual SkBitmap* GetIcon() const;
  virtual int GetButtons() const;
  virtual string16 GetButtonLabel(
     ConfirmInfoBarDelegate::InfoBarButton button) const;
  virtual bool Accept();
  virtual bool Cancel();
  virtual string16 GetLinkText();
  virtual bool LinkClicked(WindowOpenDisposition disposition);

#if defined(OS_WIN)
  // Overridden from InfoBarDelegate:
  virtual InfoBar* CreateInfoBar();
#endif  // defined(OS_WIN)

  virtual Type GetInfoBarType();

 private:
  // The AutoFillManager that initiated this InfoBar.
  AutoFillManager* host_;

  DISALLOW_COPY_AND_ASSIGN(AutoFillCCInfoBarDelegate);
};

#endif  // CHROME_BROWSER_AUTOFILL_AUTOFILL_CC_INFOBAR_DELEGATE_H_
