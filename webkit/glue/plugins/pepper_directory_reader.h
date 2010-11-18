// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBKIT_GLUE_PLUGINS_PEPPER_DIRECTORY_READER_H_
#define WEBKIT_GLUE_PLUGINS_PEPPER_DIRECTORY_READER_H_

#include <queue>

#include "base/file_util_proxy.h"
#include "webkit/glue/plugins/pepper_resource.h"

struct PP_CompletionCallback;
struct PP_DirectoryEntry_Dev;
struct PPB_DirectoryReader_Dev;

namespace pepper {

class FileRef;

class DirectoryReader : public Resource {
 public:
  explicit DirectoryReader(FileRef* directory_ref);
  virtual ~DirectoryReader();

  // Returns a pointer to the interface implementing PPB_DirectoryReader that
  // is exposed to the plugin.
  static const PPB_DirectoryReader_Dev* GetInterface();

  // Resource overrides.
  DirectoryReader* AsDirectoryReader() { return this; }

  // PPB_DirectoryReader implementation.
  int32_t GetNextEntry(PP_DirectoryEntry_Dev* entry,
                       PP_CompletionCallback callback);

  void AddNewEntries(const std::vector<base::FileUtilProxy::Entry>& entries,
                     bool has_more);

 private:
  bool FillUpEntry();

  scoped_refptr<FileRef> directory_ref_;
  std::queue<base::FileUtilProxy::Entry> entries_;
  bool has_more_;
  PP_DirectoryEntry_Dev* entry_;
};

}  // namespace pepper

#endif  // WEBKIT_GLUE_PLUGINS_PEPPER_DIRECTORY_READER_H_
