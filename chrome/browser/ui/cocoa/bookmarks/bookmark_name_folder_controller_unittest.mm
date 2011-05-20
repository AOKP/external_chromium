// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <Cocoa/Cocoa.h>

#include "base/scoped_nsobject.h"
#include "base/utf_string_conversions.h"
#import "chrome/browser/ui/cocoa/bookmarks/bookmark_name_folder_controller.h"
#include "chrome/browser/ui/cocoa/browser_test_helper.h"
#import "chrome/browser/ui/cocoa/cocoa_test_helper.h"
#include "testing/gtest/include/gtest/gtest.h"
#import "testing/gtest_mac.h"
#include "testing/platform_test.h"

class BookmarkNameFolderControllerTest : public CocoaTest {
 public:
  BrowserTestHelper helper_;
};


// Simple add of a node (at the end).
TEST_F(BookmarkNameFolderControllerTest, AddNew) {
  BookmarkModel* model = helper_.profile()->GetBookmarkModel();
  const BookmarkNode* parent = model->GetBookmarkBarNode();
  EXPECT_EQ(0, parent->GetChildCount());

  scoped_nsobject<BookmarkNameFolderController>
    controller([[BookmarkNameFolderController alloc]
                 initWithParentWindow:test_window()
                              profile:helper_.profile()
                               parent:parent
                             newIndex:0]);
  [controller window];  // force nib load

  // Do nothing.
  [controller cancel:nil];
  EXPECT_EQ(0, parent->GetChildCount());

  // Change name then cancel.
  [controller setFolderName:@"Bozo"];
  [controller cancel:nil];
  EXPECT_EQ(0, parent->GetChildCount());

  // Add a new folder.
  [controller ok:nil];
  EXPECT_EQ(1, parent->GetChildCount());
  EXPECT_TRUE(parent->GetChild(0)->is_folder());
  EXPECT_EQ(ASCIIToUTF16("Bozo"), parent->GetChild(0)->GetTitle());
}

// Add new but specify a sibling.
TEST_F(BookmarkNameFolderControllerTest, AddNewWithSibling) {
  BookmarkModel* model = helper_.profile()->GetBookmarkModel();
  const BookmarkNode* parent = model->GetBookmarkBarNode();

  // Add 2 nodes.  We will place the new folder in the middle of these.
  model->AddURL(parent, 0, ASCIIToUTF16("title 1"),
                GURL("http://www.google.com"));
  model->AddURL(parent, 1, ASCIIToUTF16("title 3"),
                GURL("http://www.google.com"));
  EXPECT_EQ(2, parent->GetChildCount());

  scoped_nsobject<BookmarkNameFolderController>
    controller([[BookmarkNameFolderController alloc]
                 initWithParentWindow:test_window()
                              profile:helper_.profile()
                               parent:parent
                             newIndex:1]);
  [controller window];  // force nib load

  // Add a new folder.
  [controller setFolderName:@"middle"];
  [controller ok:nil];

  // Confirm we now have 3, and that the new one is in the middle.
  EXPECT_EQ(3, parent->GetChildCount());
  EXPECT_TRUE(parent->GetChild(1)->is_folder());
  EXPECT_EQ(ASCIIToUTF16("middle"), parent->GetChild(1)->GetTitle());
}

// Make sure we are allowed to create a folder named "New Folder".
TEST_F(BookmarkNameFolderControllerTest, AddNewDefaultName) {
 BookmarkModel* model = helper_.profile()->GetBookmarkModel();
  const BookmarkNode* parent = model->GetBookmarkBarNode();
  EXPECT_EQ(0, parent->GetChildCount());

  scoped_nsobject<BookmarkNameFolderController>
    controller([[BookmarkNameFolderController alloc]
                 initWithParentWindow:test_window()
                              profile:helper_.profile()
                               parent:parent
                             newIndex:0]);

  [controller window];  // force nib load

  // Click OK without changing the name
  [controller ok:nil];
  EXPECT_EQ(1, parent->GetChildCount());
  EXPECT_TRUE(parent->GetChild(0)->is_folder());
}

// Make sure we are allowed to create a folder with an empty name.
TEST_F(BookmarkNameFolderControllerTest, AddNewBlankName) {
  BookmarkModel* model = helper_.profile()->GetBookmarkModel();
  const BookmarkNode* parent = model->GetBookmarkBarNode();
  EXPECT_EQ(0, parent->GetChildCount());

  scoped_nsobject<BookmarkNameFolderController>
  controller([[BookmarkNameFolderController alloc]
              initWithParentWindow:test_window()
                           profile:helper_.profile()
                            parent:parent
                          newIndex:0]);
  [controller window];  // force nib load

  // Change the name to blank, click OK.
  [controller setFolderName:@""];
  [controller ok:nil];
  EXPECT_EQ(1, parent->GetChildCount());
  EXPECT_TRUE(parent->GetChild(0)->is_folder());
}

TEST_F(BookmarkNameFolderControllerTest, Rename) {
  BookmarkModel* model = helper_.profile()->GetBookmarkModel();
  const BookmarkNode* parent = model->GetBookmarkBarNode();
  const BookmarkNode* folder = model->AddGroup(parent,
                                               parent->GetChildCount(),
                                               ASCIIToUTF16("group"));

  // Rename the folder by creating a controller that originates from
  // the node.
  scoped_nsobject<BookmarkNameFolderController>
    controller([[BookmarkNameFolderController alloc]
                 initWithParentWindow:test_window()
                              profile:helper_.profile()
                                 node:folder]);
  [controller window];  // force nib load

  EXPECT_NSEQ(@"group", [controller folderName]);
  [controller setFolderName:@"Zobo"];
  [controller ok:nil];
  EXPECT_EQ(1, parent->GetChildCount());
  EXPECT_TRUE(parent->GetChild(0)->is_folder());
  EXPECT_EQ(ASCIIToUTF16("Zobo"), parent->GetChild(0)->GetTitle());
}

TEST_F(BookmarkNameFolderControllerTest, EditAndConfirmOKButton) {
  BookmarkModel* model = helper_.profile()->GetBookmarkModel();
  const BookmarkNode* parent = model->GetBookmarkBarNode();
  EXPECT_EQ(0, parent->GetChildCount());

  scoped_nsobject<BookmarkNameFolderController>
    controller([[BookmarkNameFolderController alloc]
                 initWithParentWindow:test_window()
                              profile:helper_.profile()
                               parent:parent
                             newIndex:0]);
  [controller window];  // force nib load

  // We start enabled since the default "New Folder" is added for us.
  EXPECT_TRUE([[controller okButton] isEnabled]);

  [controller setFolderName:@"Bozo"];
  EXPECT_TRUE([[controller okButton] isEnabled]);
  [controller setFolderName:@" "];
  EXPECT_TRUE([[controller okButton] isEnabled]);

  [controller setFolderName:@""];
  EXPECT_TRUE([[controller okButton] isEnabled]);
}

