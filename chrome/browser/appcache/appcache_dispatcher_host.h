// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_APPCACHE_APPCACHE_DISPATCHER_HOST_H_
#define CHROME_BROWSER_APPCACHE_APPCACHE_DISPATCHER_HOST_H_
#pragma once

#include <vector>

#include "base/process.h"
#include "base/ref_counted.h"
#include "base/scoped_ptr.h"
#include "chrome/browser/appcache/appcache_frontend_proxy.h"
#include "chrome/browser/browser_message_filter.h"
#include "chrome/browser/renderer_host/resource_dispatcher_host.h"
#include "ipc/ipc_message.h"
#include "webkit/appcache/appcache_backend_impl.h"

class ChromeAppCacheService;
class URLRequestContext;
class URLRequestContextGetter;

// Handles appcache related messages sent to the main browser process from
// its child processes. There is a distinct host for each child process.
// Messages are handled on the IO thread. The BrowserRenderProcessHost and
// WorkerProcessHost create an instance and delegates calls to it.
class AppCacheDispatcherHost : public BrowserMessageFilter {
 public:
  // Constructor for use on the IO thread.
  AppCacheDispatcherHost(URLRequestContext* request_context,
                         int process_id);

  // Constructor for use on the UI thread.
  AppCacheDispatcherHost(URLRequestContextGetter* request_context_getter,
                         int process_id);

  ~AppCacheDispatcherHost();

  // BrowserIOMessageFilter implementation
  virtual void OnChannelConnected(int32 peer_pid);
  virtual bool OnMessageReceived(const IPC::Message& message,
                                 bool* message_was_ok);

 private:
  // BrowserMessageFilter override.
  virtual void BadMessageReceived();

  // IPC message handlers
  void OnRegisterHost(int host_id);
  void OnUnregisterHost(int host_id);
  void OnSelectCache(int host_id, const GURL& document_url,
                     int64 cache_document_was_loaded_from,
                     const GURL& opt_manifest_url);
  void OnSelectCacheForWorker(int host_id, int parent_process_id,
                              int parent_host_id);
  void OnSelectCacheForSharedWorker(int host_id, int64 appcache_id);
  void OnMarkAsForeignEntry(int host_id, const GURL& document_url,
                            int64 cache_document_was_loaded_from);
  void OnGetStatus(int host_id, IPC::Message* reply_msg);
  void OnStartUpdate(int host_id, IPC::Message* reply_msg);
  void OnSwapCache(int host_id, IPC::Message* reply_msg);
  void OnGetResourceList(
      int host_id,
      std::vector<appcache::AppCacheResourceInfo>* resource_infos);
  void GetStatusCallback(appcache::Status status, void* param);
  void StartUpdateCallback(bool result, void* param);
  void SwapCacheCallback(bool result, void* param);

  // This is only valid once Initialize() has been called.  This MUST be defined
  // before backend_impl_ since the latter maintains a (non-refcounted) pointer
  // to it.
  scoped_refptr<ChromeAppCacheService> appcache_service_;

  AppCacheFrontendProxy frontend_proxy_;
  appcache::AppCacheBackendImpl backend_impl_;

  // Temporary until OnChannelConnected() can be called from the IO thread,
  // which will extract the AppCacheService from the URLRequestContext.
  scoped_refptr<URLRequestContext> request_context_;
  scoped_refptr<URLRequestContextGetter> request_context_getter_;

  scoped_ptr<appcache::GetStatusCallback> get_status_callback_;
  scoped_ptr<appcache::StartUpdateCallback> start_update_callback_;
  scoped_ptr<appcache::SwapCacheCallback> swap_cache_callback_;
  scoped_ptr<IPC::Message> pending_reply_msg_;

  // The corresponding ChildProcessHost object's id().
  int process_id_;

  DISALLOW_COPY_AND_ASSIGN(AppCacheDispatcherHost);
};

#endif  // CHROME_BROWSER_APPCACHE_APPCACHE_DISPATCHER_HOST_H_
