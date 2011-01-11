// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_IO_THREAD_H_
#define CHROME_BROWSER_IO_THREAD_H_
#pragma once

#include <list>
#include <set>
#include <string>
#include "base/basictypes.h"
#include "base/ref_counted.h"
#include "base/scoped_ptr.h"
#include "chrome/browser/browser_process_sub_thread.h"
#include "chrome/browser/net/chrome_network_delegate.h"
#include "chrome/common/net/predictor_common.h"
#include "net/base/network_change_notifier.h"

class ChromeNetLog;
class ChromeURLRequestContextGetter;
class ListValue;
class PrefService;
class URLRequestContext;

namespace chrome_browser_net {
class ConnectInterceptor;
class Predictor;
class PrerenderInterceptor;
}  // namespace chrome_browser_net

namespace net {
class DnsRRResolver;
class HostResolver;
class HttpAuthHandlerFactory;
class ProxyScriptFetcher;
class URLSecurityManager;
}  // namespace net

class IOThread : public BrowserProcessSubThread {
 public:
  struct Globals {
    Globals();
    ~Globals();

    scoped_ptr<ChromeNetLog> net_log;
    scoped_ptr<net::HostResolver> host_resolver;
    scoped_ptr<net::DnsRRResolver> dnsrr_resolver;
    scoped_ptr<net::HttpAuthHandlerFactory> http_auth_handler_factory;
    scoped_ptr<net::URLSecurityManager> url_security_manager;
    ChromeNetworkDelegate network_delegate;
  };

  explicit IOThread(PrefService* local_state);

  virtual ~IOThread();

  // Can only be called on the IO thread.
  Globals* globals();

  // Initializes the network predictor, which induces DNS pre-resolution and/or
  // TCP/IP preconnections.  |prefetching_enabled| indicates whether or not DNS
  // prefetching should be enabled, and |preconnect_enabled| controls whether
  // TCP/IP preconnection is enabled.  This should be called by the UI thread.
  // It will post a task to the IO thread to perform the actual initialization.
  void InitNetworkPredictor(bool prefetching_enabled,
                            base::TimeDelta max_dns_queue_delay,
                            size_t max_speculative_parallel_resolves,
                            const chrome_common_net::UrlList& startup_urls,
                            ListValue* referral_list,
                            bool preconnect_enabled);

  // Registers |url_request_context_getter| into the IO thread.  During
  // IOThread::CleanUp(), IOThread will iterate through known getters and
  // release their URLRequestContexts.  Only called on the IO thread.  It does
  // not acquire a refcount for |url_request_context_getter|.  If
  // |url_request_context_getter| is being deleted before IOThread::CleanUp() is
  // invoked, then this needs to be balanced with a call to
  // UnregisterURLRequestContextGetter().
  void RegisterURLRequestContextGetter(
      ChromeURLRequestContextGetter* url_request_context_getter);

  // Unregisters |url_request_context_getter| from the IO thread.  Only called
  // on the IO thread.
  void UnregisterURLRequestContextGetter(
      ChromeURLRequestContextGetter* url_request_context_getter);

  // Handles changing to On The Record mode.  Posts a task for this onto the
  // IOThread's message loop.
  void ChangedToOnTheRecord();

  // Creates a ProxyScriptFetcherImpl which will be automatically aborted
  // during shutdown.
  // This is used to avoid cycles between the ProxyScriptFetcher and the
  // URLRequestContext that owns it (indirectly via the ProxyService).
  net::ProxyScriptFetcher* CreateAndRegisterProxyScriptFetcher(
      URLRequestContext* url_request_context);

 protected:
  virtual void Init();
  virtual void CleanUp();
  virtual void CleanUpAfterMessageLoopDestruction();

 private:
  class ManagedProxyScriptFetcher;
  typedef std::set<ManagedProxyScriptFetcher*> ProxyScriptFetchers;

  static void RegisterPrefs(PrefService* local_state);

  net::HttpAuthHandlerFactory* CreateDefaultAuthHandlerFactory(
      net::HostResolver* resolver);

  void InitNetworkPredictorOnIOThread(
      bool prefetching_enabled,
      base::TimeDelta max_dns_queue_delay,
      size_t max_speculative_parallel_resolves,
      const chrome_common_net::UrlList& startup_urls,
      ListValue* referral_list,
      bool preconnect_enabled);

  void ChangedToOnTheRecordOnIOThread();

  // These member variables are basically global, but their lifetimes are tied
  // to the IOThread.  IOThread owns them all, despite not using scoped_ptr.
  // This is because the destructor of IOThread runs on the wrong thread.  All
  // member variables should be deleted in CleanUp(), except ChromeNetLog
  // which is deleted later in CleanUpAfterMessageLoopDestruction().

  // These member variables are initialized in Init() and do not change for the
  // lifetime of the IO thread.

  Globals* globals_;

  // This variable is only meaningful during shutdown. It is used to defer
  // deletion of the NetLog to CleanUpAfterMessageLoopDestruction() even
  // though |globals_| is reset by CleanUp().
  scoped_ptr<ChromeNetLog> deferred_net_log_to_delete_;

  // Observer that logs network changes to the ChromeNetLog.
  scoped_ptr<net::NetworkChangeNotifier::Observer> network_change_observer_;

  // Store HTTP Auth-related policies in this thread.
  std::string auth_schemes_;
  bool negotiate_disable_cname_lookup_;
  bool negotiate_enable_port_;
  std::string auth_server_whitelist_;
  std::string auth_delegate_whitelist_;
  std::string gssapi_library_name_;

  // These member variables are initialized by a task posted to the IO thread,
  // which gets posted by calling certain member functions of IOThread.

  // Note: we user explicit pointers rather than smart pointers to be more
  // explicit about destruction order, and ensure that there is no chance that
  // these observers would be used accidentally after we have begun to tear
  // down.
  chrome_browser_net::ConnectInterceptor* speculative_interceptor_;
  chrome_browser_net::Predictor* predictor_;
  scoped_ptr<chrome_browser_net::PrerenderInterceptor>
    prerender_interceptor_;

  // List of live ProxyScriptFetchers.
  ProxyScriptFetchers fetchers_;

  // Keeps track of all live ChromeURLRequestContextGetters, so the
  // ChromeURLRequestContexts can be released during
  // IOThread::CleanUpAfterMessageLoopDestruction().
  std::list<ChromeURLRequestContextGetter*> url_request_context_getters_;

  DISALLOW_COPY_AND_ASSIGN(IOThread);
};

#endif  // CHROME_BROWSER_IO_THREAD_H_
