// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_IN_PROCESS_WEBKIT_INDEXED_DB_CONTEXT_H_
#define CHROME_BROWSER_IN_PROCESS_WEBKIT_INDEXED_DB_CONTEXT_H_
#pragma once

#include "base/basictypes.h"
#include "base/file_path.h"
#include "base/scoped_ptr.h"

class FilePath;
class WebKitContext;

namespace WebKit {
class WebIDBFactory;
class WebSecurityOrigin;
}

class IndexedDBContext {
 public:
  explicit IndexedDBContext(WebKitContext* webkit_context);
  ~IndexedDBContext();

  WebKit::WebIDBFactory* GetIDBFactory();

  // The indexed db directory.
  static const FilePath::CharType kIndexedDBDirectory[];

  // The indexed db file extension.
  static const FilePath::CharType kIndexedDBExtension[];

  // Get the file name of the indexed db file for the given origin and database
  // name.
  FilePath GetIndexedDBFilePath(const string16& database_name,
                                const WebKit::WebSecurityOrigin& origin) const;

  // Splits an indexed database file name into a security origin and a
  // database name.
  static bool SplitIndexedDBFileName(
      const FilePath& file_name,
      std::string* database_name,
      WebKit::WebSecurityOrigin* security_origin);

 private:
  scoped_ptr<WebKit::WebIDBFactory> idb_factory_;

  // We're owned by this WebKit context.
  WebKitContext* webkit_context_;

  DISALLOW_COPY_AND_ASSIGN(IndexedDBContext);
};

#endif  // CHROME_BROWSER_IN_PROCESS_WEBKIT_INDEXED_DB_CONTEXT_H_
