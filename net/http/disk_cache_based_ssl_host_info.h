// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_DISK_CACHE_BASED_SSL_HOST_INFO_H
#define NET_HTTP_DISK_CACHE_BASED_SSL_HOST_INFO_H

#include <string>

#include "base/lock.h"
#include "base/non_thread_safe.h"
#include "base/scoped_ptr.h"
#include "net/base/completion_callback.h"
#include "net/disk_cache/disk_cache.h"
#include "net/socket/ssl_host_info.h"

namespace net {

class IOBuffer;
class HttpCache;

// DiskCacheBasedSSLHostInfo fetches information about an SSL host from our
// standard disk cache. Since the information is defined to be non-sensitive,
// it's ok for us to keep it on disk.
class DiskCacheBasedSSLHostInfo : public SSLHostInfo,
                                  public NonThreadSafe {
 public:
  DiskCacheBasedSSLHostInfo(const std::string& hostname, HttpCache* http_cache);

  // Implementation of SSLHostInfo
  virtual void Start();
  virtual int WaitForDataReady(CompletionCallback* callback);
  virtual void Persist();

 private:
  ~DiskCacheBasedSSLHostInfo();
  std::string key() const;

  void DoLoop(int rv);

  int DoGetBackendComplete(int rv);
  int DoOpenComplete(int rv);
  int DoReadComplete(int rv);
  int DoWriteComplete(int rv);
  int DoCreateComplete(int rv);

  int DoGetBackend();
  int DoOpen();
  int DoRead();
  int DoCreate();
  int DoWrite();

  // WaitForDataReadyDone is the terminal state of the read operation.
  int WaitForDataReadyDone();
  // SetDone is the terminal state of the write operation.
  int SetDone();

  enum State {
    GET_BACKEND,
    GET_BACKEND_COMPLETE,
    OPEN,
    OPEN_COMPLETE,
    READ,
    READ_COMPLETE,
    WAIT_FOR_DATA_READY_DONE,
    CREATE,
    CREATE_COMPLETE,
    WRITE,
    WRITE_COMPLETE,
    SET_DONE,
    NONE,
  };

  scoped_refptr<CancelableCompletionCallback<DiskCacheBasedSSLHostInfo> >
      callback_;
  State state_;
  bool ready_;
  std::string new_data_;
  const std::string hostname_;
  HttpCache* const http_cache_;
  disk_cache::Backend* backend_;
  disk_cache::Entry *entry_;
  CompletionCallback* user_callback_;
  scoped_refptr<net::IOBuffer> read_buffer_;
  scoped_refptr<net::IOBuffer> write_buffer_;
  std::string data_;
};

}  // namespace net

#endif  // NET_HTTP_DISK_CACHE_BASED_SSL_HOST_INFO_H
