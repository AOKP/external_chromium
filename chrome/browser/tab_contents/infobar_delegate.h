// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_TAB_CONTENTS_INFOBAR_DELEGATE_H_
#define CHROME_BROWSER_TAB_CONTENTS_INFOBAR_DELEGATE_H_

#include <string>

#include "base/basictypes.h"
#include "chrome/browser/tab_contents/navigation_controller.h"
#include "webkit/glue/window_open_disposition.h"

class AlertInfoBarDelegate;
class ConfirmInfoBarDelegate;
class CrashedExtensionInfoBarDelegate;
class ExtensionInfoBarDelegate;
class TranslateInfoBarDelegate;
class InfoBar;
class LinkInfoBarDelegate;
class SkBitmap;
class ThemeInstalledInfoBarDelegate;

// An interface implemented by objects wishing to control an InfoBar.
// Implementing this interface is not sufficient to use an InfoBar, since it
// does not map to a specific InfoBar type. Instead, you must implement either
// AlertInfoBarDelegate or ConfirmInfoBarDelegate, or override with your own
// delegate for your own InfoBar variety.
//
// --- WARNING ---
// When creating your InfoBarDelegate subclass, it is recommended that you
// design it such that you instantiate a brand new delegate for every call to
// AddInfoBar, rather than re-using/sharing a delegate object. Otherwise,
// you need to consider the fact that more than one InfoBar instance can exist
// and reference the same delegate -- even though it is also true that we only
// ever fully show one infobar (they don't stack). The dual-references occur
// because a second InfoBar can be added while the first one is in the process
// of closing (the animations). This can cause problems because when the first
// one does finally fully close InfoBarDelegate::InfoBarClosed() is called,
// and the delegate is free to clean itself up or reset state, which may have
// fatal consequences for the InfoBar that was in the process of opening (or is
// now fully opened) -- it is referencing a delegate that may not even exist
// anymore.
// As such, it is generally much safer to dedicate a delegate instance to
// AddInfoBar!
class InfoBarDelegate {
 public:
  // The type of the infobar. It controls its appearance, such as its background
  // color.
  enum Type {
    INFO_TYPE,
    WARNING_TYPE,
    ERROR_TYPE,
    PAGE_ACTION_TYPE
  };

  // Returns true if the supplied |delegate| is equal to this one. Equality is
  // left to the implementation to define. This function is called by the
  // TabContents when determining whether or not a delegate should be added
  // because a matching one already exists. If this function returns true, the
  // TabContents will not add the new delegate because it considers one to
  // already be present.
  virtual bool EqualsDelegate(InfoBarDelegate* delegate) const {
    return false;
  }

  // Returns true if the InfoBar should be closed automatically after the page
  // is navigated. The default behavior is to return true if the page is
  // navigated somewhere else or reloaded.
  virtual bool ShouldExpire(
      const NavigationController::LoadCommittedDetails& details) const;

  // Called when the user clicks on the close button to dismiss the infobar.
  virtual void InfoBarDismissed() {}

  // Called after the InfoBar is closed. The delegate is free to delete itself
  // at this point.
  virtual void InfoBarClosed() {}

  // Called to create the InfoBar. Implementation of this method is
  // platform-specific.
  virtual InfoBar* CreateInfoBar() = 0;

  // Return the icon to be shown for this InfoBar. If the returned bitmap is
  // NULL, no icon is shown.
  virtual SkBitmap* GetIcon() const { return NULL; }

  // Returns a pointer to the AlertInfoBarDelegate interface, if implemented.
  virtual AlertInfoBarDelegate* AsAlertInfoBarDelegate() {
    return NULL;
  }

  // Returns a pointer to the LinkInfoBarDelegate interface, if implemented.
  virtual LinkInfoBarDelegate* AsLinkInfoBarDelegate() {
    return NULL;
  }

  // Returns a pointer to the ConfirmInfoBarDelegate interface, if implemented.
  virtual ConfirmInfoBarDelegate* AsConfirmInfoBarDelegate() {
    return NULL;
  }

  // Returns a pointer to the ThemeInstalledInfoBarDelegate interface, if
  // implemented.
  virtual ThemeInstalledInfoBarDelegate* AsThemePreviewInfobarDelegate() {
    return NULL;
  }

  // Returns a pointer to the TranslateInfoBarDelegate interface, if
  // implemented.
  virtual TranslateInfoBarDelegate* AsTranslateInfoBarDelegate() {
    return NULL;
  }

  // Returns a pointer to the ExtensionInfoBarDelegate interface, if
  // implemented.
  virtual ExtensionInfoBarDelegate* AsExtensionInfoBarDelegate() {
    return NULL;
  }

  // Returns a pointer to the CrashedExtensionInfoBarDelegate interface, if
  // implemented.
  virtual CrashedExtensionInfoBarDelegate* AsCrashedExtensionInfoBarDelegate() {
    return NULL;
  }

  // Returns the type of the infobar.  The type determines the appearance (such
  // as background color) of the infobar.
  virtual Type GetInfoBarType() {
    return WARNING_TYPE;
  }

 protected:
  // Provided to subclasses as a convenience to initialize the state of this
  // object. If |contents| is non-NULL, its active entry's unique ID will be
  // stored using StoreActiveEntryUniqueID automatically.
  explicit InfoBarDelegate(TabContents* contents);

  virtual ~InfoBarDelegate() { }

  // Store the unique id for the active entry in the specified TabContents, to
  // be used later upon navigation to determine if this InfoBarDelegate should
  // be expired from |contents_|.
  void StoreActiveEntryUniqueID(TabContents* contents);

 private:
  // The unique id of the active NavigationEntry of the TabContents taht we were
  // opened for. Used to help expire on navigations.
  int contents_unique_id_;

  DISALLOW_COPY_AND_ASSIGN(InfoBarDelegate);
};

// An interface derived from InfoBarDelegate implemented by objects wishing to
// control an AlertInfoBar.
class AlertInfoBarDelegate : public InfoBarDelegate {
 public:
  // Returns the message string to be displayed for the InfoBar.
  virtual std::wstring GetMessageText() const = 0;

  // Overridden from InfoBarDelegate.
  virtual SkBitmap* GetIcon() const { return NULL; }

  // Overridden from InfoBarDelegate:
  virtual bool EqualsDelegate(InfoBarDelegate* delegate) const;
  virtual InfoBar* CreateInfoBar();
  virtual AlertInfoBarDelegate* AsAlertInfoBarDelegate() { return this; }

 protected:
  explicit AlertInfoBarDelegate(TabContents* contents);

  DISALLOW_COPY_AND_ASSIGN(AlertInfoBarDelegate);
};

// An interface derived from InfoBarDelegate implemented by objects wishing to
// control a LinkInfoBar.
class LinkInfoBarDelegate : public InfoBarDelegate {
 public:
  // Returns the message string to be displayed in the InfoBar. |link_offset|
  // is the position where the link should be inserted. If |link_offset| is set
  // to std::wstring::npos (it is by default), the link is right aligned within
  // the InfoBar rather than being embedded in the message text.
  virtual std::wstring GetMessageTextWithOffset(size_t* link_offset) const {
    *link_offset = std::wstring::npos;
    return std::wstring();
  }

  // Returns the text of the link to be displayed.
  virtual std::wstring GetLinkText() const = 0;

  // Overridden from InfoBarDelegate.
  virtual SkBitmap* GetIcon() const { return NULL; }

  // Called when the Link is clicked. The |disposition| specifies how the
  // resulting document should be loaded (based on the event flags present when
  // the link was clicked). This function returns true if the InfoBar should be
  // closed now or false if it should remain until the user explicitly closes
  // it.
  virtual bool LinkClicked(WindowOpenDisposition disposition) {
    return true;
  }

  // Overridden from InfoBarDelegate:
  virtual InfoBar* CreateInfoBar();
  virtual LinkInfoBarDelegate* AsLinkInfoBarDelegate() {
    return this;
  }

 protected:
  explicit LinkInfoBarDelegate(TabContents* contents);

  DISALLOW_COPY_AND_ASSIGN(LinkInfoBarDelegate);
};

// An interface derived from InfoBarDelegate implemented by objects wishing to
// control a ConfirmInfoBar.
class ConfirmInfoBarDelegate : public AlertInfoBarDelegate {
 public:
  enum InfoBarButton {
    BUTTON_NONE = 0,
    BUTTON_OK = 1,
    BUTTON_CANCEL = 2,
    // Specifies that the OK button should be rendered like a default button.
    BUTTON_OK_DEFAULT = 4
  };

  // Return the buttons to be shown for this InfoBar.
  virtual int GetButtons() const {
    return BUTTON_NONE;
  }

  // Return the label for the specified button. The default implementation
  // returns "OK" for the OK button and "Cancel" for the Cancel button.
  virtual std::wstring GetButtonLabel(InfoBarButton button) const;

  // Return whether or not the specified button needs elevation.
  virtual bool NeedElevation(InfoBarButton button) const { return false; }

  // Called when the OK button is pressed. If the function returns true, the
  // InfoBarDelegate should be removed from the associated TabContents.
  virtual bool Accept() { return true; }

  // Called when the Cancel button is pressed.  If the function returns true,
  // the InfoBarDelegate should be removed from the associated TabContents.
  virtual bool Cancel() { return true; }

  // Returns the text of the link to be displayed, if any. Otherwise returns
  // and empty string.
  virtual std::wstring GetLinkText() {
    return std::wstring();
  }

  // Called when the Link is clicked. The |disposition| specifies how the
  // resulting document should be loaded (based on the event flags present when
  // the link was clicked). This function returns true if the InfoBar should be
  // closed now or false if it should remain until the user explicitly closes
  // it.
  // Will only be called if GetLinkText() returns non-empty string.
  virtual bool LinkClicked(WindowOpenDisposition disposition) {
    return true;
  }

  // Overridden from InfoBarDelegate:
  virtual InfoBar* CreateInfoBar();
  virtual ConfirmInfoBarDelegate* AsConfirmInfoBarDelegate() {
    return this;
  }

 protected:
  explicit ConfirmInfoBarDelegate(TabContents* contents);

  DISALLOW_COPY_AND_ASSIGN(ConfirmInfoBarDelegate);
};

// Simple implementations for common use cases ---------------------------------

class SimpleAlertInfoBarDelegate : public AlertInfoBarDelegate {
 public:
  // |icon| may be |NULL|.
  SimpleAlertInfoBarDelegate(TabContents* contents,
                             const std::wstring& message,
                             SkBitmap* icon,
                             bool auto_expire);

  // Overridden from AlertInfoBarDelegate:
  virtual bool ShouldExpire(
      const NavigationController::LoadCommittedDetails& details) const;
  virtual std::wstring GetMessageText() const;
  virtual SkBitmap* GetIcon() const;
  virtual void InfoBarClosed();

 private:
  std::wstring message_;
  SkBitmap* icon_;
  bool auto_expire_;  // Should it expire automatically on navigation?

  DISALLOW_COPY_AND_ASSIGN(SimpleAlertInfoBarDelegate);
};

#endif  // CHROME_BROWSER_TAB_CONTENTS_INFOBAR_DELEGATE_H_
