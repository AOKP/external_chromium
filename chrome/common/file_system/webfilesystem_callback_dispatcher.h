// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_COMMON_FILE_SYSTEM_WEBFILESYSTEM_CALLBACK_DISPATCHER_H_
#define CHROME_COMMON_FILE_SYSTEM_WEBFILESYSTEM_CALLBACK_DISPATCHER_H_

#include "base/basictypes.h"
#include "base/platform_file.h"
#include "webkit/fileapi/file_system_callback_dispatcher.h"

namespace base {
namespace file_util_proxy {
struct Entry;
}
}

namespace WebKit {
class WebFileSystemCallbacks;
}

class WebFileSystemCallbackDispatcher
    : public fileapi::FileSystemCallbackDispatcher {
 public:
  explicit WebFileSystemCallbackDispatcher(
      WebKit::WebFileSystemCallbacks* callbacks);

  // FileSystemCallbackDispatcher implementation
  virtual void DidSucceed();
  virtual void DidReadMetadata(const base::PlatformFileInfo& file_info);
  virtual void DidReadDirectory(
      const std::vector<base::file_util_proxy::Entry>& entries,
      bool has_more);
  virtual void DidOpenFileSystem(const std::string&,
                                 const FilePath&);
  virtual void DidFail(base::PlatformFileError);
  virtual void DidWrite(int64 bytes, bool complete);

 private:
  WebKit::WebFileSystemCallbacks* callbacks_;
};

#endif  // CHROME_COMMON_FILE_SYSTEM_WEBFILESYSTEM_CALLBACK_DISPATCHER_H_
