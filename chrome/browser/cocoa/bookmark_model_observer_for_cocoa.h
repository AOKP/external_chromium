// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// C++ bridge class to send a selector to a Cocoa object when the
// bookmark model changes.  Some Cocoa objects edit the bookmark model
// and temporarily save a copy of the state (e.g. bookmark button
// editor).  As a fail-safe, these objects want an easy cancel if the
// model changes out from under them.  For example, if you have the
// bookmark button editor sheet open, then edit the bookmark in the
// bookmark manager, we'd want to simply cancel the editor.
//
// This class is conservative and may result in notifications which
// aren't strictly necessary.  For example, node removal only needs to
// cancel an edit if the removed node is a folder (editors often have
// a list of "new parents").  But, just to be sure, notification
// happens on any removal.

#ifndef CHROME_BROWSER_COCOA_BOOKMARK_MODEL_OBSERVER_FOR_COCOA_H
#define CHROME_BROWSER_COCOA_BOOKMARK_MODEL_OBSERVER_FOR_COCOA_H
#pragma once

#import <Cocoa/Cocoa.h>

#include "base/basictypes.h"
#include "base/scoped_nsobject.h"
#include "chrome/browser/bookmarks/bookmark_model.h"
#include "chrome/browser/bookmarks/bookmark_model_observer.h"

class BookmarkModelObserverForCocoa : public BookmarkModelObserver {
 public:
  // When |node| in |model| changes, send |selector| to |object|.
  // Assumes |selector| is a selector that takes one arg, like an
  // IBOutlet.  The arg passed is nil.
  // Many notifications happen independently of node
  // (e.g. BeingDeleted), so |node| can be nil.
  //
  // |object| is NOT retained, since the expected use case is for
  // ||object| to own the BookmarkModelObserverForCocoa and we don't
  // want a retain cycle.
  BookmarkModelObserverForCocoa(const BookmarkNode* node,
                                BookmarkModel* model,
                                NSObject* object,
                                SEL selector) {
    DCHECK(model);
    node_ = node;
    model_ = model;
    object_ = object;
    selector_ = selector;
    model_->AddObserver(this);
  }
  virtual ~BookmarkModelObserverForCocoa() {
    model_->RemoveObserver(this);
  }

  virtual void BookmarkModelBeingDeleted(BookmarkModel* model) {
    Notify();
  }
  virtual void BookmarkNodeMoved(BookmarkModel* model,
                                 const BookmarkNode* old_parent,
                                 int old_index,
                                 const BookmarkNode* new_parent,
                                 int new_index) {
    // Editors often have a tree of parents, so movement of folders
    // must cause a cancel.
      Notify();
  }
  virtual void BookmarkNodeRemoved(BookmarkModel* model,
                                   const BookmarkNode* parent,
                                   int old_index,
                                   const BookmarkNode* node) {
    // See comment in BookmarkNodeMoved.
    Notify();
  }
  virtual void BookmarkNodeChanged(BookmarkModel* model,
                                   const BookmarkNode* node) {
    if ((node_ == node) || (!node_))
      Notify();
  }
  virtual void BookmarkImportBeginning(BookmarkModel* model) {
    // Be conservative.
    Notify();
  }

  // Some notifications we don't care about, but by being pure virtual
  // in the base class we must implement them.
  virtual void Loaded(BookmarkModel* model) {
  }
  virtual void BookmarkNodeAdded(BookmarkModel* model,
                                 const BookmarkNode* parent,
                                 int index) {
  }
  virtual void BookmarkNodeFavIconLoaded(BookmarkModel* model,
                                         const BookmarkNode* node) {
  }
  virtual void BookmarkNodeChildrenReordered(BookmarkModel* model,
                                             const BookmarkNode* node) {
  }

  virtual void BookmarkImportEnding(BookmarkModel* model) {
  }

 private:
  const BookmarkNode* node_;  // Weak; owned by a BookmarkModel.
  BookmarkModel* model_;  // Weak; it is owned by a Profile.
  NSObject* object_; // Weak, like a delegate.
  SEL selector_;

  void Notify() {
    [object_ performSelector:selector_ withObject:nil];
  }

  DISALLOW_COPY_AND_ASSIGN(BookmarkModelObserverForCocoa);
};

#endif  // CHROME_BROWSER_COCOA_BOOKMARK_MODEL_OBSERVER_FOR_COCOA_H

