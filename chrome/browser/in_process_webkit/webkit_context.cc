// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/in_process_webkit/webkit_context.h"

#include "base/command_line.h"
#include "chrome/browser/browser_thread.h"
#include "chrome/browser/profile.h"
#include "chrome/common/chrome_switches.h"

WebKitContext::WebKitContext(Profile* profile)
    : data_path_(profile->IsOffTheRecord() ? FilePath() : profile->GetPath()),
      is_incognito_(profile->IsOffTheRecord()),
      ALLOW_THIS_IN_INITIALIZER_LIST(
          dom_storage_context_(new DOMStorageContext(this))),
      ALLOW_THIS_IN_INITIALIZER_LIST(
          indexed_db_context_(new IndexedDBContext(this))) {
}

WebKitContext::~WebKitContext() {
  // If the WebKit thread was ever spun up, delete the object there.  The task
  // will just get deleted if the WebKit thread isn't created (which only
  // happens during testing).
  DOMStorageContext* dom_storage_context = dom_storage_context_.release();
  if (!BrowserThread::DeleteSoon(
          BrowserThread::WEBKIT, FROM_HERE, dom_storage_context)) {
    // The WebKit thread wasn't created, and the task got deleted without
    // freeing the DOMStorageContext, so delete it manually.
    delete dom_storage_context;
  }

  IndexedDBContext* indexed_db_context = indexed_db_context_.release();
  if (!BrowserThread::DeleteSoon(
          BrowserThread::WEBKIT, FROM_HERE, indexed_db_context)) {
    delete indexed_db_context;
  }
}

void WebKitContext::PurgeMemory() {
  if (!BrowserThread::CurrentlyOn(BrowserThread::WEBKIT)) {
    bool result = BrowserThread::PostTask(
        BrowserThread::WEBKIT, FROM_HERE,
        NewRunnableMethod(this, &WebKitContext::PurgeMemory));
    DCHECK(result);
    return;
  }

  dom_storage_context_->PurgeMemory();
}

void WebKitContext::DeleteDataModifiedSince(
    const base::Time& cutoff,
    const char* url_scheme_to_be_skipped,
    const std::vector<string16>& protected_origins) {
  if (!BrowserThread::CurrentlyOn(BrowserThread::WEBKIT)) {
    bool result = BrowserThread::PostTask(
        BrowserThread::WEBKIT, FROM_HERE,
        NewRunnableMethod(this, &WebKitContext::DeleteDataModifiedSince,
                          cutoff, url_scheme_to_be_skipped, protected_origins));
    DCHECK(result);
    return;
  }

  dom_storage_context_->DeleteDataModifiedSince(
      cutoff, url_scheme_to_be_skipped, protected_origins);
}


void WebKitContext::DeleteSessionStorageNamespace(
    int64 session_storage_namespace_id) {
  if (!BrowserThread::CurrentlyOn(BrowserThread::WEBKIT)) {
    BrowserThread::PostTask(
        BrowserThread::WEBKIT, FROM_HERE,
        NewRunnableMethod(this, &WebKitContext::DeleteSessionStorageNamespace,
                          session_storage_namespace_id));
    return;
  }

  dom_storage_context_->DeleteSessionStorageNamespace(
      session_storage_namespace_id);
}
