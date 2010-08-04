// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_bookmark_helpers.h"

#include "base/string_util.h"
#include "chrome/browser/extensions/extension_bookmarks_module_constants.h"

namespace keys = extension_bookmarks_module_constants;

// Helper functions.
namespace extension_bookmark_helpers {

DictionaryValue* GetNodeDictionary(const BookmarkNode* node,
                                   bool recurse,
                                   bool only_folders) {
  DictionaryValue* dict = new DictionaryValue();
  dict->SetString(keys::kIdKey, Int64ToString(node->id()));

  const BookmarkNode* parent = node->GetParent();
  if (parent) {
    dict->SetString(keys::kParentIdKey, Int64ToString(parent->id()));
    dict->SetInteger(keys::kIndexKey, parent->IndexOfChild(node));
  }

  if (!node->is_folder()) {
    dict->SetString(keys::kUrlKey, node->GetURL().spec());
  } else {
    // Javascript Date wants milliseconds since the epoch, ToDoubleT is
    // seconds.
    base::Time t = node->date_group_modified();
    if (!t.is_null())
      dict->SetReal(keys::kDateGroupModifiedKey, floor(t.ToDoubleT() * 1000));
  }

  dict->SetString(keys::kTitleKey, node->GetTitle());
  if (!node->date_added().is_null()) {
    // Javascript Date wants milliseconds since the epoch, ToDoubleT is
    // seconds.
    dict->SetReal(keys::kDateAddedKey,
                  floor(node->date_added().ToDoubleT() * 1000));
  }

  if (recurse && node->is_folder()) {
    int childCount = node->GetChildCount();
    ListValue* children = new ListValue();
    for (int i = 0; i < childCount; ++i) {
      const BookmarkNode* child = node->GetChild(i);
      if (!only_folders || child->is_folder()) {
        DictionaryValue* dict = GetNodeDictionary(child, true, only_folders);
        children->Append(dict);
      }
    }
    dict->Set(keys::kChildrenKey, children);
  }
  return dict;
}

void AddNode(const BookmarkNode* node,
             ListValue* list,
             bool recurse,
             bool only_folders) {
  DictionaryValue* dict = GetNodeDictionary(node, recurse, only_folders);
  list->Append(dict);
}

// Add a JSON representation of |node| to the JSON |list|.
void AddNode(const BookmarkNode* node,
             ListValue* list,
             bool recurse) {
  return AddNode(node, list, recurse, false);
}

void AddNodeFoldersOnly(const BookmarkNode* node,
             ListValue* list,
             bool recurse) {
  return AddNode(node, list, recurse, true);
}

bool RemoveNode(BookmarkModel* model,
                int64 id,
                bool recursive,
                std::string* error) {
  const BookmarkNode* node = model->GetNodeByID(id);
  if (!node) {
    *error = keys::kNoNodeError;
    return false;
  }
  if (node == model->root_node() ||
      node == model->other_node() ||
      node == model->GetBookmarkBarNode()) {
    *error = keys::kModifySpecialError;
    return false;
  }
  if (node->is_folder() && node->GetChildCount() > 0 && !recursive) {
    *error = keys::kFolderNotEmptyError;
    return false;
  }

  const BookmarkNode* parent = node->GetParent();
  int index = parent->IndexOfChild(node);
  model->Remove(parent, index);
  return true;
}

}
