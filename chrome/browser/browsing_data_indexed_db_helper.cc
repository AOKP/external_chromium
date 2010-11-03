// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/browsing_data_indexed_db_helper.h"

#include "base/file_util.h"
#include "base/message_loop.h"
#include "base/scoped_ptr.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/browser_thread.h"
#include "chrome/browser/in_process_webkit/webkit_context.h"
#include "chrome/browser/profile.h"
#include "third_party/WebKit/WebKit/chromium/public/WebCString.h"
#include "third_party/WebKit/WebKit/chromium/public/WebSecurityOrigin.h"
#include "third_party/WebKit/WebKit/chromium/public/WebString.h"
#include "webkit/glue/webkit_glue.h"

using WebKit::WebSecurityOrigin;

namespace {

class BrowsingDataIndexedDBHelperImpl : public BrowsingDataIndexedDBHelper {
 public:
  explicit BrowsingDataIndexedDBHelperImpl(Profile* profile);

  virtual void StartFetching(
      Callback1<const std::vector<IndexedDBInfo>& >::Type* callback);
  virtual void CancelNotification();
  virtual void DeleteIndexedDBFile(const FilePath& file_path);

 private:
  virtual ~BrowsingDataIndexedDBHelperImpl();

  // Enumerates all indexed database files in the WEBKIT thread.
  void FetchIndexedDBInfoInWebKitThread();
  // Notifies the completion callback in the UI thread.
  void NotifyInUIThread();
  // Delete a single indexed database file in the WEBKIT thread.
  void DeleteIndexedDBFileInWebKitThread(const FilePath& file_path);

  Profile* profile_;

  // This only mutates in the WEBKIT thread.
  std::vector<IndexedDBInfo> indexed_db_info_;

  // This only mutates on the UI thread.
  scoped_ptr<Callback1<const std::vector<IndexedDBInfo>& >::Type >
      completion_callback_;
  // Indicates whether or not we're currently fetching information:
  // it's true when StartFetching() is called in the UI thread, and it's reset
  // after we notified the callback in the UI thread.
  // This only mutates on the UI thread.
  bool is_fetching_;

  DISALLOW_COPY_AND_ASSIGN(BrowsingDataIndexedDBHelperImpl);
};

BrowsingDataIndexedDBHelperImpl::BrowsingDataIndexedDBHelperImpl(
    Profile* profile)
    : profile_(profile),
      completion_callback_(NULL),
      is_fetching_(false) {
  DCHECK(profile_);
}

BrowsingDataIndexedDBHelperImpl::~BrowsingDataIndexedDBHelperImpl() {
}

void BrowsingDataIndexedDBHelperImpl::StartFetching(
    Callback1<const std::vector<IndexedDBInfo>& >::Type* callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!is_fetching_);
  DCHECK(callback);
  is_fetching_ = true;
  completion_callback_.reset(callback);
  BrowserThread::PostTask(
      BrowserThread::WEBKIT, FROM_HERE,
      NewRunnableMethod(
          this,
          &BrowsingDataIndexedDBHelperImpl::FetchIndexedDBInfoInWebKitThread));
}

void BrowsingDataIndexedDBHelperImpl::CancelNotification() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  completion_callback_.reset(NULL);
}

void BrowsingDataIndexedDBHelperImpl::DeleteIndexedDBFile(
    const FilePath& file_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  BrowserThread::PostTask(
      BrowserThread::WEBKIT, FROM_HERE,
       NewRunnableMethod(
           this,
           &BrowsingDataIndexedDBHelperImpl::
              DeleteIndexedDBFileInWebKitThread,
           file_path));
}

void BrowsingDataIndexedDBHelperImpl::FetchIndexedDBInfoInWebKitThread() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::WEBKIT));
  file_util::FileEnumerator file_enumerator(
      profile_->GetWebKitContext()->data_path().Append(
          IndexedDBContext::kIndexedDBDirectory),
      false, file_util::FileEnumerator::FILES);
  for (FilePath file_path = file_enumerator.Next(); !file_path.empty();
       file_path = file_enumerator.Next()) {
    if (file_path.Extension() == IndexedDBContext::kIndexedDBExtension) {
      std::string name;
      WebSecurityOrigin web_security_origin;
      if (!IndexedDBContext::SplitIndexedDBFileName(
          file_path, &name, &web_security_origin)) {
        // Could not parse file name.
        continue;
      }
      if (EqualsASCII(web_security_origin.protocol(),
                      chrome::kExtensionScheme)) {
        // Extension state is not considered browsing data.
        continue;
      }
      base::PlatformFileInfo file_info;
      bool ret = file_util::GetFileInfo(file_path, &file_info);
      if (ret) {
        indexed_db_info_.push_back(IndexedDBInfo(
            web_security_origin.protocol().utf8(),
            web_security_origin.host().utf8(),
            web_security_origin.port(),
            web_security_origin.databaseIdentifier().utf8(),
            web_security_origin.toString().utf8(),
            name,
            file_path,
            file_info.size,
            file_info.last_modified));
      }
    }
  }

  BrowserThread::PostTask(
      BrowserThread::UI, FROM_HERE,
      NewRunnableMethod(
          this, &BrowsingDataIndexedDBHelperImpl::NotifyInUIThread));
}

void BrowsingDataIndexedDBHelperImpl::NotifyInUIThread() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(is_fetching_);
  // Note: completion_callback_ mutates only in the UI thread, so it's safe to
  // test it here.
  if (completion_callback_ != NULL) {
    completion_callback_->Run(indexed_db_info_);
    completion_callback_.reset();
  }
  is_fetching_ = false;
}

void BrowsingDataIndexedDBHelperImpl::DeleteIndexedDBFileInWebKitThread(
    const FilePath& file_path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::WEBKIT));
  // TODO(jochen): implement this once it's possible to delete indexed DBs.
}

}  // namespace

// static
BrowsingDataIndexedDBHelper* BrowsingDataIndexedDBHelper::Create(
    Profile* profile) {
  return new BrowsingDataIndexedDBHelperImpl(profile);
}

CannedBrowsingDataIndexedDBHelper::CannedBrowsingDataIndexedDBHelper(
    Profile* profile)
    : profile_(profile) {
  DCHECK(profile);
}

void CannedBrowsingDataIndexedDBHelper::AddIndexedDB(
    const GURL& origin, const string16& name, const string16& description) {
  WebSecurityOrigin web_security_origin =
      WebSecurityOrigin::createFromString(
          UTF8ToUTF16(origin.spec()));
  std::string security_origin(web_security_origin.toString().utf8());

  for (std::vector<IndexedDBInfo>::iterator
       indexed_db = indexed_db_info_.begin();
       indexed_db != indexed_db_info_.end(); ++indexed_db) {
    if (indexed_db->origin == security_origin)
      return;
  }

  indexed_db_info_.push_back(IndexedDBInfo(
      web_security_origin.protocol().utf8(),
      web_security_origin.host().utf8(),
      web_security_origin.port(),
      web_security_origin.databaseIdentifier().utf8(),
      security_origin,
      UTF16ToUTF8(name),
      profile_->GetWebKitContext()->indexed_db_context()->
          GetIndexedDBFilePath(name, web_security_origin),
      0,
      base::Time()));
}

void CannedBrowsingDataIndexedDBHelper::Reset() {
  indexed_db_info_.clear();
}

bool CannedBrowsingDataIndexedDBHelper::empty() const {
  return indexed_db_info_.empty();
}

void CannedBrowsingDataIndexedDBHelper::StartFetching(
    Callback1<const std::vector<IndexedDBInfo>& >::Type* callback) {
  callback->Run(indexed_db_info_);
  delete callback;
}

CannedBrowsingDataIndexedDBHelper::~CannedBrowsingDataIndexedDBHelper() {}
