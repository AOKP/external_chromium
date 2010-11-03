// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/disk_cache_based_ssl_host_info.h"

#include "base/callback.h"
#include "base/logging.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/http/http_cache.h"

namespace net {

DiskCacheBasedSSLHostInfo::DiskCacheBasedSSLHostInfo(
    const std::string& hostname, HttpCache* http_cache)
    : callback_(new CancelableCompletionCallback<DiskCacheBasedSSLHostInfo>(
                        ALLOW_THIS_IN_INITIALIZER_LIST(this),
                        &DiskCacheBasedSSLHostInfo::DoLoop)),
      state_(GET_BACKEND),
      ready_(false),
      hostname_(hostname),
      http_cache_(http_cache),
      backend_(NULL),
      entry_(NULL),
      user_callback_(NULL) {
}

void DiskCacheBasedSSLHostInfo::Start() {
  DCHECK(CalledOnValidThread());
  DCHECK_EQ(GET_BACKEND, state_);
  DoLoop(OK);
}

DiskCacheBasedSSLHostInfo::~DiskCacheBasedSSLHostInfo() {
  DCHECK(!user_callback_);
  if (entry_)
    entry_->Close();
  callback_->Cancel();
}

std::string DiskCacheBasedSSLHostInfo::key() const {
  return "sslhostinfo:" + hostname_;
}

void DiskCacheBasedSSLHostInfo::DoLoop(int rv) {
  do {
    switch (state_) {
      case GET_BACKEND:
        rv = DoGetBackend();
        break;
      case GET_BACKEND_COMPLETE:
        rv = DoGetBackendComplete(rv);
        break;
      case OPEN:
        rv = DoOpen();
        break;
      case OPEN_COMPLETE:
        rv = DoOpenComplete(rv);
        break;
      case READ:
        rv = DoRead();
        break;
      case READ_COMPLETE:
        rv = DoReadComplete(rv);
        break;
      case WAIT_FOR_DATA_READY_DONE:
        rv = WaitForDataReadyDone();
        break;
      case CREATE:
        rv = DoCreate();
        break;
      case CREATE_COMPLETE:
        rv = DoCreateComplete(rv);
        break;
      case WRITE:
        rv = DoWrite();
        break;
      case WRITE_COMPLETE:
        rv = DoWriteComplete(rv);
        break;
      case SET_DONE:
        rv = SetDone();
        break;
      default:
        rv = OK;
        NOTREACHED();
    }
  } while (rv != ERR_IO_PENDING && state_ != NONE);
}

int DiskCacheBasedSSLHostInfo::DoGetBackend() {
  state_ = GET_BACKEND_COMPLETE;
  return http_cache_->GetBackend(&backend_, callback_.get());
}

int DiskCacheBasedSSLHostInfo::DoGetBackendComplete(int rv) {
  if (rv == OK) {
    state_ = OPEN;
  } else {
    state_ = WAIT_FOR_DATA_READY_DONE;
  }
  return OK;
}

int DiskCacheBasedSSLHostInfo::DoOpen() {
  state_ = OPEN_COMPLETE;
  return backend_->OpenEntry(key(), &entry_, callback_.get());
}

int DiskCacheBasedSSLHostInfo::DoOpenComplete(int rv) {
  if (rv == OK) {
    state_ = READ;
  } else {
    state_ = WAIT_FOR_DATA_READY_DONE;
  }

  return OK;
}

int DiskCacheBasedSSLHostInfo::DoRead() {
  const int32 size = entry_->GetDataSize(0 /* index */);
  if (!size) {
    state_ = WAIT_FOR_DATA_READY_DONE;
    return OK;
  }

  read_buffer_ = new IOBuffer(size);
  state_ = READ_COMPLETE;
  return entry_->ReadData(0 /* index */, 0 /* offset */, read_buffer_,
                          size, callback_.get());
}

int DiskCacheBasedSSLHostInfo::DoReadComplete(int rv) {
  if (rv > 0)
    data_ = std::string(read_buffer_->data(), rv);

  state_ = WAIT_FOR_DATA_READY_DONE;
  return OK;
}

int DiskCacheBasedSSLHostInfo::WaitForDataReadyDone() {
  CompletionCallback* callback;

  DCHECK(!ready_);
  state_ = NONE;
  ready_ = true;
  callback = user_callback_;
  user_callback_ = NULL;
  // We close the entry because, if we shutdown before ::Persist is called,
  // then we might leak a cache reference, which causes a DCHECK on shutdown.
  if (entry_)
    entry_->Close();
  entry_ = NULL;
  Parse(data_);

  if (callback)
    callback->Run(OK);

  return OK;
}

int DiskCacheBasedSSLHostInfo::WaitForDataReady(CompletionCallback* callback) {
  DCHECK(CalledOnValidThread());
  DCHECK(state_ != GET_BACKEND);

  if (ready_)
    return OK;
  if (callback) {
    DCHECK(!user_callback_);
    user_callback_ = callback;
  }
  return ERR_IO_PENDING;
}

void DiskCacheBasedSSLHostInfo::Persist() {
  DCHECK(CalledOnValidThread());
  DCHECK(state_ != GET_BACKEND);

  DCHECK(new_data_.empty());
  CHECK(ready_);
  DCHECK(user_callback_ == NULL);
  new_data_ = Serialize();

  if (!backend_)
    return;

  state_ = CREATE;
  DoLoop(OK);
}

int DiskCacheBasedSSLHostInfo::DoCreate() {
  DCHECK(entry_ == NULL);
  state_ = CREATE_COMPLETE;
  return backend_->CreateEntry(key(), &entry_, callback_.get());
}

int DiskCacheBasedSSLHostInfo::DoCreateComplete(int rv) {
  if (rv != OK) {
    state_ = SET_DONE;
  } else {
    state_ = WRITE;
  }
  return OK;
}

int DiskCacheBasedSSLHostInfo::DoWrite() {
  write_buffer_ = new IOBuffer(new_data_.size());
  memcpy(write_buffer_->data(), new_data_.data(), new_data_.size());
  state_ = WRITE_COMPLETE;
  return entry_->WriteData(0 /* index */, 0 /* offset */, write_buffer_,
                           new_data_.size(), callback_.get(),
                           true /* truncate */);
}

int DiskCacheBasedSSLHostInfo::DoWriteComplete(int rv) {
  state_ = SET_DONE;
  return OK;
}

int DiskCacheBasedSSLHostInfo::SetDone() {
  if (entry_)
    entry_->Close();
  entry_ = NULL;
  state_ = NONE;
  return OK;
}

} // namespace net
