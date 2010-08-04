// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_IN_PROCESS_WEBKIT_INDEXED_DB_CONTEXT_H_
#define CHROME_BROWSER_IN_PROCESS_WEBKIT_INDEXED_DB_CONTEXT_H_

#include "base/basictypes.h"
#include "base/logging.h"
#include "base/scoped_ptr.h"

namespace WebKit {
class WebIndexedDatabase;
}

class IndexedDBContext {
 public:
  IndexedDBContext();
  ~IndexedDBContext();

  // TODO(jorlow): If this is all this class ever does, then we should kill it
  //               and move this variable to WebKitContext.
  WebKit::WebIndexedDatabase* GetIndexedDatabase();

 private:
  scoped_ptr<WebKit::WebIndexedDatabase> indexed_database_;

  DISALLOW_COPY_AND_ASSIGN(IndexedDBContext);
};

#endif  // CHROME_BROWSER_IN_PROCESS_WEBKIT_INDEXED_DB_CONTEXT_H_
