// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/bookmarks/recently_used_folders_combo_model.h"

#include "app/l10n_util.h"
#include "chrome/browser/bookmarks/bookmark_utils.h"
#include "grit/generated_resources.h"

namespace {

// Max number of most recently used folders.
const size_t kMaxMRUFolders = 5;

}  // namespace

RecentlyUsedFoldersComboModel::RecentlyUsedFoldersComboModel(
    BookmarkModel* model, const BookmarkNode* node)
    // Use + 2 to account for bookmark bar and other node.
    : nodes_(bookmark_utils::GetMostRecentlyModifiedGroups(
          model, kMaxMRUFolders + 2)),
      node_parent_index_(0) {
  // TODO(sky): bug 1173415 add a separator in the combobox here.

  // We special case the placement of these, so remove them from the list, then
  // fix up the order.
  RemoveNode(model->GetBookmarkBarNode());
  RemoveNode(model->other_node());
  RemoveNode(node->GetParent());

  // Make the parent the first item, unless it's the bookmark bar or other node.
  if (node->GetParent() != model->GetBookmarkBarNode() &&
      node->GetParent() != model->other_node()) {
    nodes_.insert(nodes_.begin(), node->GetParent());
  }

  // Make sure we only have kMaxMRUFolders in the first chunk.
  if (nodes_.size() > kMaxMRUFolders)
    nodes_.erase(nodes_.begin() + kMaxMRUFolders, nodes_.end());

  // And put the bookmark bar and other nodes at the end of the list.
  nodes_.push_back(model->GetBookmarkBarNode());
  nodes_.push_back(model->other_node());

  std::vector<const BookmarkNode*>::iterator it = std::find(nodes_.begin(),
                                                            nodes_.end(),
                                                            node->GetParent());
  node_parent_index_ = static_cast<int>(it - nodes_.begin());
}

RecentlyUsedFoldersComboModel::~RecentlyUsedFoldersComboModel() {}

int RecentlyUsedFoldersComboModel::GetItemCount() {
  return static_cast<int>(nodes_.size() + 1);
}

string16 RecentlyUsedFoldersComboModel::GetItemAt(int index) {
  if (index == static_cast<int>(nodes_.size()))
    return l10n_util::GetStringUTF16(IDS_BOOMARK_BUBBLE_CHOOSER_ANOTHER_FOLDER);
  return nodes_[index]->GetTitle();
}

const BookmarkNode* RecentlyUsedFoldersComboModel::GetNodeAt(int index) {
  if (index < 0 || index >= static_cast<int>(nodes_.size()))
    return NULL;
  return nodes_[index];
}

void RecentlyUsedFoldersComboModel::RemoveNode(const BookmarkNode* node) {
  std::vector<const BookmarkNode*>::iterator it = std::find(nodes_.begin(),
                                                            nodes_.end(),
                                                            node);
  if (it != nodes_.end())
    nodes_.erase(it);
}
