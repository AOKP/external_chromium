// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_BOOKMARKS_MODULE_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_BOOKMARKS_MODULE_H_
#pragma once

#include <list>
#include <set>
#include <string>

#include "base/singleton.h"
#include "chrome/browser/bookmarks/bookmark_model_observer.h"
#include "chrome/browser/extensions/extension_function.h"
#include "chrome/browser/shell_dialogs.h"
#include "chrome/common/notification_observer.h"
#include "chrome/common/notification_registrar.h"

// Observes BookmarkModel and then routes the notifications as events to
// the extension system.
class ExtensionBookmarkEventRouter : public BookmarkModelObserver {
 public:
  static ExtensionBookmarkEventRouter* GetSingleton();
  virtual ~ExtensionBookmarkEventRouter();

  // Call this for each model to observe.  Safe to call multiple times per
  // model.
  void Observe(BookmarkModel* model);

  // BookmarkModelObserver
  virtual void Loaded(BookmarkModel* model);
  virtual void BookmarkModelBeingDeleted(BookmarkModel* model) { }
  virtual void BookmarkNodeMoved(BookmarkModel* model,
                                 const BookmarkNode* old_parent,
                                 int old_index,
                                 const BookmarkNode* new_parent,
                                 int new_index);
  virtual void BookmarkNodeAdded(BookmarkModel* model,
                                 const BookmarkNode* parent,
                                 int index);
  virtual void BookmarkNodeRemoved(BookmarkModel* model,
                                   const BookmarkNode* parent,
                                   int old_index,
                                   const BookmarkNode* node);
  virtual void BookmarkNodeChanged(BookmarkModel* model,
                                   const BookmarkNode* node);
  virtual void BookmarkNodeFavIconLoaded(BookmarkModel* model,
                                         const BookmarkNode* node);
  virtual void BookmarkNodeChildrenReordered(BookmarkModel* model,
                                             const BookmarkNode* node);
  virtual void BookmarkImportBeginning(BookmarkModel* model);
  virtual void BookmarkImportEnding(BookmarkModel* model);

 private:
  ExtensionBookmarkEventRouter();
  friend struct DefaultSingletonTraits<ExtensionBookmarkEventRouter>;

  // Helper to actually dispatch an event to extension listeners.
  void DispatchEvent(Profile* profile,
                     const char* event_name,
                     const std::string json_args);

  // These are stored so that Observe can be called multiple times safely.
  // This way the caller doesn't have to know whether it's already observing
  // a particular model or not.  The pointers are not owned by this object.
  std::set<BookmarkModel*> models_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionBookmarkEventRouter);
};

class BookmarksFunction : public AsyncExtensionFunction,
                          public NotificationObserver {
 public:
  virtual void Run();
  virtual bool RunImpl() = 0;

 protected:
  // Helper to get the bookmark id as int64 from the given string id.
  // Sets error_ to an error string if the given id string can't be parsed
  // as an int64. In case of error, doesn't change id and returns false.
  bool GetBookmarkIdAsInt64(const std::string& id_string, int64* id);

 private:
  virtual void Observe(NotificationType type,
                       const NotificationSource& source,
                       const NotificationDetails& details);
  NotificationRegistrar registrar_;
};

class GetBookmarksFunction : public BookmarksFunction {
 public:
  virtual bool RunImpl();
 private:
  DECLARE_EXTENSION_FUNCTION_NAME("bookmarks.get")
};

class GetBookmarkChildrenFunction : public BookmarksFunction {
 public:
  virtual bool RunImpl();
 private:
  DECLARE_EXTENSION_FUNCTION_NAME("bookmarks.getChildren")
};

class GetBookmarkRecentFunction : public BookmarksFunction {
 public:
  virtual bool RunImpl();
 private:
  DECLARE_EXTENSION_FUNCTION_NAME("bookmarks.getRecent")
};

class GetBookmarkTreeFunction : public BookmarksFunction {
 public:
  virtual bool RunImpl();
 private:
  DECLARE_EXTENSION_FUNCTION_NAME("bookmarks.getTree")
};

class SearchBookmarksFunction : public BookmarksFunction {
 public:
  virtual bool RunImpl();
 private:
  DECLARE_EXTENSION_FUNCTION_NAME("bookmarks.search")
};

class RemoveBookmarkFunction : public BookmarksFunction {
 public:
  // Returns true on successful parse and sets invalid_id to true if conversion
  // from id string to int64 failed.
  static bool ExtractIds(const ListValue* args, std::list<int64>* ids,
                         bool* invalid_id);
  virtual bool RunImpl();
  virtual void GetQuotaLimitHeuristics(
      std::list<QuotaLimitHeuristic*>* heuristics) const;
 private:
  DECLARE_EXTENSION_FUNCTION_NAME("bookmarks.remove")
};

class RemoveTreeBookmarkFunction : public RemoveBookmarkFunction {
  DECLARE_EXTENSION_FUNCTION_NAME("bookmarks.removeTree")
};

class CreateBookmarkFunction : public BookmarksFunction {
 public:
  virtual void GetQuotaLimitHeuristics(
      std::list<QuotaLimitHeuristic*>* heuristics) const;
  virtual bool RunImpl();
 private:
  DECLARE_EXTENSION_FUNCTION_NAME("bookmarks.create")
};

class MoveBookmarkFunction : public BookmarksFunction {
 public:
  static bool ExtractIds(const ListValue* args, std::list<int64>* ids,
                         bool* invalid_id);
  virtual void GetQuotaLimitHeuristics(
      std::list<QuotaLimitHeuristic*>* heuristics) const;
  virtual bool RunImpl();
 private:
  DECLARE_EXTENSION_FUNCTION_NAME("bookmarks.move")
};

class UpdateBookmarkFunction : public BookmarksFunction {
 public:
  static bool ExtractIds(const ListValue* args, std::list<int64>* ids,
                         bool* invalid_id);
  virtual void GetQuotaLimitHeuristics(
      std::list<QuotaLimitHeuristic*>* heuristics) const;
  virtual bool RunImpl();
 private:
  DECLARE_EXTENSION_FUNCTION_NAME("bookmarks.update")
};

class BookmarksIOFunction : public BookmarksFunction,
                            public SelectFileDialog::Listener {
 public:
  BookmarksIOFunction();
  virtual ~BookmarksIOFunction();

  // Overridden from SelectFileDialog::Listener:
  virtual void FileSelected(const FilePath& path, int index, void* params) = 0;
  void MultiFilesSelected(const std::vector<FilePath>& files, void* params);
  void FileSelectionCanceled(void* params);
  void SelectFile(SelectFileDialog::Type type);

 protected:
  scoped_refptr<SelectFileDialog> select_file_dialog_;
};

class ImportBookmarksFunction : public BookmarksIOFunction {
 public:
  // Override BookmarkManagerIOFunction.
  bool RunImpl();
  void FileSelected(const FilePath& path, int index, void* params);

 private:
  DECLARE_EXTENSION_FUNCTION_NAME("bookmarks.import");
};

class ExportBookmarksFunction : public BookmarksIOFunction {
 public:
  // Override BookmarkManagerIOFunction.
  bool RunImpl();
  void FileSelected(const FilePath& path, int index, void* params);

 private:
  DECLARE_EXTENSION_FUNCTION_NAME("bookmarks.export");
};

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_BOOKMARKS_MODULE_H_
