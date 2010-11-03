// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_STATUS_INPUT_METHOD_MENU_H_
#define CHROME_BROWSER_CHROMEOS_STATUS_INPUT_METHOD_MENU_H_
#pragma once

#include "app/menus/simple_menu_model.h"
#include "chrome/browser/chromeos/cros/input_method_library.h"
#include "chrome/browser/prefs/pref_member.h"
#include "chrome/common/notification_observer.h"
#include "chrome/common/notification_registrar.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/notification_type.h"
#include "views/controls/menu/menu_2.h"
#include "views/controls/menu/view_menu_delegate.h"

class PrefService;
class SkBitmap;

namespace chromeos {

// A class for the dropdown menu for switching input method and keyboard layout.
// Since the class provides the views::ViewMenuDelegate interface, it's easy to
// create a button widget (e.g. views::MenuButton, chromeos::StatusAreaButton)
// which shows the dropdown menu on click.
class InputMethodMenu : public views::ViewMenuDelegate,
                        public menus::MenuModel,
                        public InputMethodLibrary::Observer,
                        public NotificationObserver {
 public:
  InputMethodMenu(PrefService* pref_service,
                  bool is_browser_mode,
                  bool is_screen_locker);
  virtual ~InputMethodMenu();

  // menus::MenuModel implementation.
  virtual bool HasIcons() const;
  virtual int GetItemCount() const;
  virtual menus::MenuModel::ItemType GetTypeAt(int index) const;
  virtual int GetCommandIdAt(int index) const;
  virtual string16 GetLabelAt(int index) const;
  virtual bool IsLabelDynamicAt(int index) const;
  virtual bool GetAcceleratorAt(int index,
                                menus::Accelerator* accelerator) const;
  virtual bool IsItemCheckedAt(int index) const;
  virtual int GetGroupIdAt(int index) const;
  virtual bool GetIconAt(int index, SkBitmap* icon) const;
  virtual menus::ButtonMenuItemModel* GetButtonMenuItemAt(int index) const;
  virtual bool IsEnabledAt(int index) const;
  virtual menus::MenuModel* GetSubmenuModelAt(int index) const;
  virtual void HighlightChangedTo(int index);
  virtual void ActivatedAt(int index);
  virtual void MenuWillShow();

  // views::ViewMenuDelegate implementation. Sub classes can override the method
  // to adjust the position of the menu.
  virtual void RunMenu(views::View* unused_source,
                       const gfx::Point& pt);

  // InputMethodLibrary::Observer implementation.
  virtual void InputMethodChanged(InputMethodLibrary* obj);
  virtual void ImePropertiesChanged(InputMethodLibrary* obj);
  virtual void ActiveInputMethodsChanged(InputMethodLibrary* obj);

  // NotificationObserver implementation.
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);

  // Sets the minimum width of the dropdown menu.
  void SetMinimumWidth(int width);

  // Registers input method preferences for the login screen.
  static void RegisterPrefs(PrefService* local_state);

  // Returns a string for the indicator on top right corner of the Chrome
  // window. The method is public for unit tests.
  static std::wstring GetTextForIndicator(
      const InputMethodDescriptor& input_method);

  // Returns a string for the drop-down menu and the tooltip for the indicator.
  // The method is public for unit tests.
  static std::wstring GetTextForMenu(const InputMethodDescriptor& input_method);

 protected:
  // Parses |input_method| and then calls UpdateUI().
  void UpdateUIFromInputMethod(const InputMethodDescriptor& input_method);

  // Rebuilds model and menu2 objects in preparetion to open the menu.
  void PrepareForMenuOpen();

  // Returns menu2 object for language menu.
  views::Menu2& language_menu() {
    return language_menu_;
  }

 private:
  // Updates UI of a container of the menu (e.g. the "US" menu button in the
  // status area). Sub classes have to implement the interface for their own UI.
  virtual void UpdateUI(
      const std::wstring& name, const std::wstring& tooltip) = 0;

  // Sub classes have to implement the interface. This interface should return
  // true if the dropdown menu should show an item like "Customize languages
  // and input..." DOMUI.
  virtual bool ShouldSupportConfigUI() = 0;

  // Sub classes have to implement the interface which opens an UI for
  // customizing languages and input.
  virtual void OpenConfigUI() = 0;

  // Rebuilds |model_|. This function should be called whenever
  // |input_method_descriptors_| is updated, or ImePropertiesChanged() is
  // called.
  void RebuildModel();

  // Returns true if the zero-origin |index| points to one of the input methods.
  bool IndexIsInInputMethodList(int index) const;

  // Returns true if the zero-origin |index| points to one of the IME
  // properties. When returning true, |property_index| is updated so that
  // property_list.at(property_index) points to the menu item.
  bool GetPropertyIndex(int index, int* property_index) const;

  // Returns true if the zero-origin |index| points to the "Configure IME" menu
  // item.
  bool IndexPointsToConfigureImeMenuItem(int index) const;

  // The current input method list.
  scoped_ptr<InputMethodDescriptors> input_method_descriptors_;

  // Objects for reading/writing the Chrome prefs.
  StringPrefMember previous_input_method_pref_;
  StringPrefMember current_input_method_pref_;

  // We borrow menus::SimpleMenuModel implementation to maintain the current
  // content of the pop-up menu. The menus::MenuModel is implemented using this
  // |model_|.
  scoped_ptr<menus::SimpleMenuModel> model_;

  // The language menu which pops up when the button in status area is clicked.
  views::Menu2 language_menu_;
  int minimum_language_menu_width_;

  PrefService* pref_service_;
  NotificationRegistrar registrar_;
  bool logged_in_;
  const bool is_browser_mode_;
  const bool is_screen_locker_mode_;

  DISALLOW_COPY_AND_ASSIGN(InputMethodMenu);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_STATUS_INPUT_METHOD_MENU_H_
