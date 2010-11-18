// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_VIEWS_NATIVE_MENU_DOMUI_H_
#define CHROME_BROWSER_CHROMEOS_VIEWS_NATIVE_MENU_DOMUI_H_
#pragma once

#include <vector>

#include "base/message_loop.h"
#include "base/observer_list.h"
#include "base/scoped_ptr.h"
#include "chrome/browser/chromeos/dom_ui/domui_menu_control.h"
#include "googleurl/src/gurl.h"
#include "views/controls/menu/menu_wrapper.h"

class SkBitmap;
class DictionaryValue;
class Profile;

namespace menus {
class MenuModel;
}  // namespace menus

namespace views {
class NestedDispatcherGtk;
}  // namespace views;

#if defined(TOUCH_UI)
typedef union _XEvent XEvent;
#endif

namespace chromeos {

class MenuLocator;
class DOMUIMenuWidget;

// A DOMUI implementation of MenuWrapper.
class NativeMenuDOMUI : public views::MenuWrapper,
                        public DOMUIMenuControl,
                        public MessageLoop::Dispatcher {
 public:
  NativeMenuDOMUI(menus::MenuModel* menu_model, bool root);
  virtual ~NativeMenuDOMUI();

  // Returns true if menu is currently shown.
  bool is_menu_shown() { return menu_shown_; }

  // Set parent menu.
  void set_parent(NativeMenuDOMUI* parent) { parent_ = parent; }

  // Overridden from MenuWrapper:
  virtual void RunMenuAt(const gfx::Point& point, int alignment);
  virtual void CancelMenu();
  virtual void Rebuild();
  virtual void UpdateStates();
  virtual gfx::NativeMenu GetNativeMenu() const;
  virtual MenuAction GetMenuAction() const;
  virtual void AddMenuListener(views::MenuListener* listener);
  virtual void RemoveMenuListener(views::MenuListener* listener);
  virtual void SetMinimumWidth(int width);

  // Overriden from MessageLoopForUI::Dispatcher:
  virtual bool Dispatch(GdkEvent* event);
#if defined(TOUCH_UI)
  virtual bool Dispatch(XEvent* xevent);
#endif

  // Overriden from DOMUIMenuControl;
  virtual menus::MenuModel* GetMenuModel() { return model_; }
  virtual void Activate(menus::MenuModel* model,
                        int index,
                        ActivationMode activation_mode);
  virtual void CloseAll();
  virtual void CloseSubmenu();
  virtual void MoveInputToParent();
  virtual void MoveInputToSubmenu();
  virtual void OnLoad();
  virtual void OpenSubmenu(int index, int y);
  virtual void SetSize(const gfx::Size& size);

  // Hide All menu (including submenus).
  void Hide();

  // Returns the root of the menu tree. Returns NULL if it cannot find
  // a root. (i.e. detached from root)
  NativeMenuDOMUI* GetRoot();

  // Returns the profile to create DOMView.
  Profile* GetProfile();

  // Called when the menu is ready to accept input.
  // Used in interactive_ui_test to wait for menu opened.
  void InputIsReady();

  // Sets/Gets the url for the domui menu.
  void set_menu_url(const GURL& url) { menu_url_ = url; }
  const GURL& menu_url() const { return menu_url_; }

  // Sets the menu url of menu2. This has to be called before
  // RunMenuAt/RunContextMenuAt is called.
  static void SetMenuURL(views::Menu2* menu2, const GURL& url);

 private:
  // Callback that we should really process the menu activation.
  // See description above class for why we delay processing activation.
  void ProcessActivate();

  // Show the menu using given |locator|.
  void ShowAt(MenuLocator* locator);

  // Find a menu object at point.
  NativeMenuDOMUI* FindMenuAt(const gfx::Point& point);

  // If we're a submenu, this is the parent.
  NativeMenuDOMUI* parent_;

  // Holds the current submenu.
  scoped_ptr<NativeMenuDOMUI> submenu_;

  menus::MenuModel* model_;

  // A window widget that draws the content of the menu.
  DOMUIMenuWidget* menu_widget_;

  // True if the menu is currently shown.
  // Used only in root.
  bool menu_shown_;

  // If the user selects something from the menu this is the menu they selected
  // it from. When an item is selected menu_activated_ on the root ancestor is
  // set to the menu the user selected and after the nested message loop exits
  // Activate is invoked on this menu.
  menus::MenuModel* activated_menu_;

  // The index of the item the user selected. This is set on the
  // actual menu the user selects and not the root.
  int activated_index_;

  // The action that took place during the call to RunMenuAt.
  MenuAction menu_action_;

  // Vector of listeners to receive callbacks when the menu opens.
  ObserverList<views::MenuListener> listeners_;

  // URL to invoke Menu DOMUI. Default menu is chrome://menu, but
  // custom menu can use different url using SetMenuURL method
  // (e.g. chrome://wrench-menu for wrench menu).
  GURL menu_url_;

  // A guard flag to avoid calling MenuListener::OnMenuOpened twice.
  bool on_menu_opened_called_;

  // Nested dispatcher object that can outlive this object.
  // This is to deal with the menu being deleted while the nested
  // message loop is handled. see http://crosbug.com/7929 .
  views::NestedDispatcherGtk* nested_dispatcher_;

  DISALLOW_COPY_AND_ASSIGN(NativeMenuDOMUI);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_VIEWS_NATIVE_MENU_DOMUI_H_
