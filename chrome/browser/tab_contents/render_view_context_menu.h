// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_TAB_CONTENTS_RENDER_VIEW_CONTEXT_MENU_H_
#define CHROME_BROWSER_TAB_CONTENTS_RENDER_VIEW_CONTEXT_MENU_H_

#include <map>
#include <string>
#include <vector>

#include "app/menus/simple_menu_model.h"
#include "base/string16.h"
#include "base/scoped_vector.h"
#include "chrome/common/page_transition_types.h"
#include "chrome/browser/extensions/extension_menu_manager.h"
#include "webkit/glue/context_menu.h"
#include "webkit/glue/window_open_disposition.h"

class ExtensionMenuItem;
class Profile;
class TabContents;

namespace gfx {
class Point;
}

namespace WebKit {
struct WebMediaPlayerAction;
}

class RenderViewContextMenu : public menus::SimpleMenuModel::Delegate {
 public:
  RenderViewContextMenu(TabContents* tab_contents,
                        const ContextMenuParams& params);

  virtual ~RenderViewContextMenu();

  // Initializes the context menu.
  void Init();

  // SimpleMenuModel::Delegate implementation.
  virtual bool IsCommandIdChecked(int command_id) const;
  virtual bool IsCommandIdEnabled(int command_id) const;
  virtual void ExecuteCommand(int command_id);

 protected:
  void InitMenu();

  // Platform specific functions.
  virtual void PlatformInit() = 0;
  virtual bool GetAcceleratorForCommandId(
      int command_id,
      menus::Accelerator* accelerator) = 0;

  // Attempts to get an ExtensionMenuItem given the id of a context menu item.
  ExtensionMenuItem* GetExtensionMenuItem(int id) const;

  ContextMenuParams params_;
  TabContents* source_tab_contents_;
  Profile* profile_;

  menus::SimpleMenuModel menu_model_;

  // True if we are showing for an external tab contents. The default is false.
  bool external_;


  // Maps the id from a context menu item to the ExtensionMenuItem's internal
  // id.
  std::map<int, ExtensionMenuItem::Id> extension_item_map_;

 private:
  static bool IsDevToolsURL(const GURL& url);
  static bool IsSyncResourcesURL(const GURL& url);
  bool AppendCustomItems();
  void AppendDeveloperItems();
  void AppendLinkItems();
  void AppendImageItems();
  void AppendAudioItems();
  void AppendVideoItems();
  void AppendMediaItems();
  void AppendPageItems();
  void AppendFrameItems();
  void AppendCopyItem();
  void AppendEditableItems();
  void AppendSearchProvider();
  void AppendAllExtensionItems();
  void AppendSpellcheckOptionsSubMenu();
  // Add writing direction sub menu (only used on Mac).
  void AppendBidiSubMenu();

  // This is a helper function to append items for one particular extension.
  // The |index| parameter is used for assigning id's, and is incremented for
  // each item actually added.
  void AppendExtensionItems(const std::string& extension_id, int* index);

  // Used for recursively adding submenus of extension items.
  void RecursivelyAppendExtensionItems(
      const std::vector<ExtensionMenuItem*>& items,
      menus::SimpleMenuModel* menu_model,
      int *index);
  // This will set the icon on the most recently-added item in the menu_model_.
  void SetExtensionIcon(const std::string& extension_id);

  // Opens the specified URL string in a new tab.  If |in_current_window| is
  // false, a new window is created to hold the new tab.
  void OpenURL(const GURL& url,
               WindowOpenDisposition disposition,
               PageTransition::Type transition);

  // Copy to the clipboard an image located at a point in the RenderView
  void CopyImageAt(int x, int y);

  // Launch the inspector targeting a point in the RenderView
  void Inspect(int x, int y);

  // Writes the specified text/url to the system clipboard
  void WriteURLToClipboard(const GURL& url);

  void MediaPlayerActionAt(const gfx::Point& location,
                           const WebKit::WebMediaPlayerAction& action);

  bool IsDevCommandEnabled(int id) const;

  // Returns a (possibly truncated) version of the current selection text
  // suitable or putting in the title of a menu item.
  string16 PrintableSelectionText();

  // The destination URL to use if the user tries to search for or navigate to
  // a text selection.
  GURL selection_navigation_url_;

  menus::SimpleMenuModel spellcheck_submenu_model_;
  menus::SimpleMenuModel bidi_submenu_model_;
  ScopedVector<menus::SimpleMenuModel> extension_menu_models_;

  DISALLOW_COPY_AND_ASSIGN(RenderViewContextMenu);
};

#endif  // CHROME_BROWSER_TAB_CONTENTS_RENDER_VIEW_CONTEXT_MENU_H_
