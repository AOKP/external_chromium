// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_VIEWS_BOOKMARK_BUBBLE_VIEW_H_
#define CHROME_BROWSER_VIEWS_BOOKMARK_BUBBLE_VIEW_H_

#include <vector>

#include "app/combobox_model.h"
#include "chrome/browser/bookmarks/recently_used_folders_combo_model.h"
#include "chrome/browser/views/info_bubble.h"
#include "gfx/rect.h"
#include "googleurl/src/gurl.h"
#include "views/controls/button/button.h"
#include "views/controls/combobox/combobox.h"
#include "views/controls/link.h"
#include "views/view.h"

class Profile;

class BookmarkModel;
class BookmarkNode;

namespace views {
class NativeButton;
class Textfield;
}

// BookmarkBubbleView is a view intended to be used as the content of an
// InfoBubble. BookmarkBubbleView provides views for unstarring and editting
// the bookmark it is created with. Don't create a BookmarkBubbleView directly,
// instead use the static Show method.
class BookmarkBubbleView : public views::View,
                           public views::LinkController,
                           public views::ButtonListener,
                           public views::Combobox::Listener,
                           public InfoBubbleDelegate {
 public:
  static void Show(views::Window* window,
                   const gfx::Rect& bounds,
                   InfoBubbleDelegate* delegate,
                   Profile* profile,
                   const GURL& url,
                   bool newly_bookmarked);

  static bool IsShowing();

  static void Hide();

  virtual ~BookmarkBubbleView();

  void set_info_bubble(InfoBubble* info_bubble) { info_bubble_ = info_bubble; }

  // Overridden to force a layout.
  virtual void DidChangeBounds(const gfx::Rect& previous,
                               const gfx::Rect& current);

  // Invoked after the bubble has been shown.
  virtual void BubbleShown();

  // Override to close on return.
  virtual bool AcceleratorPressed(const views::Accelerator& accelerator);

  virtual void ViewHierarchyChanged(bool is_add, View* parent, View* child);

 private:
  // Creates a BookmarkBubbleView.
  // |title| is the title of the page. If newly_bookmarked is false, title is
  // ignored and the title of the bookmark is fetched from the database.
  BookmarkBubbleView(InfoBubbleDelegate* delegate,
                     Profile* profile,
                     const GURL& url,
                     bool newly_bookmarked);
  // Creates the child views.
  void Init();

  // Returns the title to display.
  std::wstring GetTitle();

  // LinkController method, either unstars the item or shows the bookmark
  // editor (depending upon which link was clicked).
  virtual void LinkActivated(views::Link* source, int event_flags);

  // ButtonListener method, closes the bubble or opens the edit dialog.
  virtual void ButtonPressed(views::Button* sender, const views::Event& event);

  // Combobox::Listener method. Changes the parent of the bookmark.
  virtual void ItemChanged(views::Combobox* combobox,
                           int prev_index,
                           int new_index);

  // InfoBubbleDelegate methods. These forward to the InfoBubbleDelegate
  // supplied in the constructor as well as sending out the necessary
  // notification.
  virtual void InfoBubbleClosing(InfoBubble* info_bubble,
                                 bool closed_by_escape);
  virtual bool CloseOnEscape();
  virtual bool FadeInOnShow() { return false; }
  virtual std::wstring accessible_name();

  // Closes the bubble.
  void Close();

  // Handle the message when the user presses a button.
  void HandleButtonPressed(views::Button* sender);

  // Shows the BookmarkEditor.
  void ShowEditor();

  // Sets the title and parent of the node.
  void ApplyEdits();

  // The bookmark bubble, if we're showing one.
  static BookmarkBubbleView* bubble_;

  // The InfoBubble showing us.
  InfoBubble* info_bubble_;

  // Delegate for the bubble, may be null.
  InfoBubbleDelegate* delegate_;

  // The profile.
  Profile* profile_;

  // The bookmark URL.
  const GURL url_;

  // Title of the bookmark. This is initially the title supplied to the
  // constructor, which is typically the title of the page.
  std::wstring title_;

  // If true, the page was just bookmarked.
  const bool newly_bookmarked_;

  RecentlyUsedFoldersComboModel parent_model_;

  // Link for removing/unstarring the bookmark.
  views::Link* remove_link_;

  // Button to bring up the editor.
  views::NativeButton* edit_button_;

  // Button to close the window.
  views::NativeButton* close_button_;

  // Textfield showing the title of the bookmark.
  views::Textfield* title_tf_;

  // Combobox showing a handful of folders the user can choose from, including
  // the current parent.
  views::Combobox* parent_combobox_;

  // When the destructor is invoked should the bookmark be removed?
  bool remove_bookmark_;

  // When the destructor is invoked should edits be applied?
  bool apply_edits_;

  DISALLOW_COPY_AND_ASSIGN(BookmarkBubbleView);
};

#endif  // CHROME_BROWSER_VIEWS_BOOKMARK_BUBBLE_VIEW_H_
