// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/io_thread.h"

#include "base/command_line.h"
#include "base/debug/leak_tracker.h"
#include "base/logging.h"
#include "base/metrics/field_trial.h"
#include "base/stl_util-inl.h"
#include "base/string_number_conversions.h"
#include "base/string_split.h"
#include "base/string_util.h"
#include "base/thread_restrictions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_thread.h"
#include "chrome/browser/gpu_process_host.h"
#include "chrome/browser/net/chrome_net_log.h"
#include "chrome/browser/net/chrome_url_request_context.h"
#include "chrome/browser/net/connect_interceptor.h"
#include "chrome/browser/net/passive_log_collector.h"
#include "chrome/browser/net/predictor_api.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/net/raw_host_resolver_proc.h"
#include "chrome/common/net/url_fetcher.h"
#include "net/base/dnsrr_resolver.h"
#include "net/base/host_cache.h"
#include "net/base/host_resolver.h"
#include "net/base/host_resolver_impl.h"
#include "net/base/mapped_host_resolver.h"
#include "net/base/net_util.h"
#include "net/http/http_auth_filter.h"
#include "net/http/http_auth_handler_factory.h"
#if defined(USE_NSS)
#include "net/ocsp/nss_ocsp.h"
#endif  // defined(USE_NSS)
#include "net/proxy/proxy_script_fetcher_impl.h"

namespace {

net::HostResolver* CreateGlobalHostResolver(net::NetLog* net_log) {
  const CommandLine& command_line = *CommandLine::ForCurrentProcess();

  size_t parallelism = net::HostResolver::kDefaultParallelism;

  // Use the concurrency override from the command-line, if any.
  if (command_line.HasSwitch(switches::kHostResolverParallelism)) {
    std::string s =
        command_line.GetSwitchValueASCII(switches::kHostResolverParallelism);

    // Parse the switch (it should be a positive integer formatted as decimal).
    int n;
    if (base::StringToInt(s, &n) && n > 0) {
      parallelism = static_cast<size_t>(n);
    } else {
      LOG(ERROR) << "Invalid switch for host resolver parallelism: " << s;
    }
  } else {
    // Set up a field trial to see what impact the total number of concurrent
    // resolutions have on DNS resolutions.
    base::FieldTrial::Probability kDivisor = 1000;
    // For each option (i.e., non-default), we have a fixed probability.
    base::FieldTrial::Probability kProbabilityPerGroup = 100;  // 10%.

    scoped_refptr<base::FieldTrial> trial(
        new base::FieldTrial("DnsParallelism", kDivisor));

    // List options with different counts.
    // Firefox limits total to 8 in parallel, and default is currently 50.
    int parallel_6 = trial->AppendGroup("parallel_6", kProbabilityPerGroup);
    int parallel_7 = trial->AppendGroup("parallel_7", kProbabilityPerGroup);
    int parallel_8 = trial->AppendGroup("parallel_8", kProbabilityPerGroup);
    int parallel_9 = trial->AppendGroup("parallel_9", kProbabilityPerGroup);
    int parallel_10 = trial->AppendGroup("parallel_10", kProbabilityPerGroup);
    int parallel_14 = trial->AppendGroup("parallel_14", kProbabilityPerGroup);
    int parallel_20 = trial->AppendGroup("parallel_20", kProbabilityPerGroup);

    trial->AppendGroup("parallel_default",
                        base::FieldTrial::kAllRemainingProbability);

    if (trial->group() == parallel_6)
      parallelism = 6;
    else if (trial->group() == parallel_7)
      parallelism = 7;
    else if (trial->group() == parallel_8)
      parallelism = 8;
    else if (trial->group() == parallel_9)
      parallelism = 9;
    else if (trial->group() == parallel_10)
      parallelism = 10;
    else if (trial->group() == parallel_14)
      parallelism = 14;
    else if (trial->group() == parallel_20)
      parallelism = 20;
  }

  // Use the specified DNS server for doing raw resolutions if requested
  // from the command-line.
  scoped_refptr<net::HostResolverProc> resolver_proc;
  if (command_line.HasSwitch(switches::kDnsServer)) {
    std::string dns_ip_string =
        command_line.GetSwitchValueASCII(switches::kDnsServer);
    net::IPAddressNumber dns_ip_number;
    if (net::ParseIPLiteralToNumber(dns_ip_string, &dns_ip_number)) {
      resolver_proc =
          new chrome_common_net::RawHostResolverProc(dns_ip_number, NULL);
    } else {
      LOG(ERROR) << "Invalid IP address specified for --dns-server: "
                 << dns_ip_string;
    }
  }

  net::HostResolver* global_host_resolver =
      net::CreateSystemHostResolver(parallelism, resolver_proc.get(), net_log);

  // Determine if we should disable IPv6 support.
  if (!command_line.HasSwitch(switches::kEnableIPv6)) {
    if (command_line.HasSwitch(switches::kDisableIPv6)) {
      global_host_resolver->SetDefaultAddressFamily(net::ADDRESS_FAMILY_IPV4);
    } else {
      net::HostResolverImpl* host_resolver_impl =
          global_host_resolver->GetAsHostResolverImpl();
      if (host_resolver_impl != NULL) {
        // Use probe to decide if support is warranted.
        host_resolver_impl->ProbeIPv6Support();
      }
    }
  }

  // If hostname remappings were specified on the command-line, layer these
  // rules on top of the real host resolver. This allows forwarding all requests
  // through a designated test server.
  if (!command_line.HasSwitch(switches::kHostResolverRules))
    return global_host_resolver;

  net::MappedHostResolver* remapped_resolver =
      new net::MappedHostResolver(global_host_resolver);
  remapped_resolver->SetRulesFromString(
      command_line.GetSwitchValueASCII(switches::kHostResolverRules));
  return remapped_resolver;
}

class LoggingNetworkChangeObserver
    : public net::NetworkChangeNotifier::Observer {
 public:
  // |net_log| must remain valid throughout our lifetime.
  explicit LoggingNetworkChangeObserver(net::NetLog* net_log)
      : net_log_(net_log) {
    net::NetworkChangeNotifier::AddObserver(this);
  }

  ~LoggingNetworkChangeObserver() {
    net::NetworkChangeNotifier::RemoveObserver(this);
  }

  virtual void OnIPAddressChanged() {
    VLOG(1) << "Observed a change to the network IP addresses";

    net_log_->AddEntry(net::NetLog::TYPE_NETWORK_IP_ADDRESSES_CHANGED,
                       base::TimeTicks::Now(),
                       net::NetLog::Source(),
                       net::NetLog::PHASE_NONE,
                       NULL);
  }

 private:
  net::NetLog* net_log_;
  DISALLOW_COPY_AND_ASSIGN(LoggingNetworkChangeObserver);
};

}  // namespace

// This is a wrapper class around ProxyScriptFetcherImpl that will
// keep track of live instances.
class IOThread::ManagedProxyScriptFetcher
    : public net::ProxyScriptFetcherImpl {
 public:
  ManagedProxyScriptFetcher(URLRequestContext* context,
                            IOThread* io_thread)
      : net::ProxyScriptFetcherImpl(context),
        io_thread_(io_thread) {
    DCHECK(!ContainsKey(*fetchers(), this));
    fetchers()->insert(this);
  }

  virtual ~ManagedProxyScriptFetcher() {
    DCHECK(ContainsKey(*fetchers(), this));
    fetchers()->erase(this);
  }

 private:
  ProxyScriptFetchers* fetchers() {
    return &io_thread_->fetchers_;
  }

  IOThread* io_thread_;

  DISALLOW_COPY_AND_ASSIGN(ManagedProxyScriptFetcher);
};

// The IOThread object must outlive any tasks posted to the IO thread before the
// Quit task.
DISABLE_RUNNABLE_METHOD_REFCOUNT(IOThread);

IOThread::Globals::Globals() {}

IOThread::Globals::~Globals() {}

IOThread::IOThread()
    : BrowserProcessSubThread(BrowserThread::IO),
      globals_(NULL),
      speculative_interceptor_(NULL),
      predictor_(NULL) {}

IOThread::~IOThread() {
  // We cannot rely on our base class to stop the thread since we want our
  // CleanUp function to run.
  Stop();
  DCHECK(!globals_);
}

IOThread::Globals* IOThread::globals() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  return globals_;
}

void IOThread::InitNetworkPredictor(
    bool prefetching_enabled,
    base::TimeDelta max_dns_queue_delay,
    size_t max_speculative_parallel_resolves,
    const chrome_common_net::UrlList& startup_urls,
    ListValue* referral_list,
    bool preconnect_enabled) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  message_loop()->PostTask(
      FROM_HERE,
      NewRunnableMethod(
          this,
          &IOThread::InitNetworkPredictorOnIOThread,
          prefetching_enabled, max_dns_queue_delay,
          max_speculative_parallel_resolves,
          startup_urls, referral_list, preconnect_enabled));
}

void IOThread::RegisterURLRequestContextGetter(
    ChromeURLRequestContextGetter* url_request_context_getter) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  std::list<ChromeURLRequestContextGetter*>::const_iterator it =
      std::find(url_request_context_getters_.begin(),
                url_request_context_getters_.end(),
                url_request_context_getter);
  DCHECK(it == url_request_context_getters_.end());
  url_request_context_getters_.push_back(url_request_context_getter);
}

void IOThread::UnregisterURLRequestContextGetter(
    ChromeURLRequestContextGetter* url_request_context_getter) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  std::list<ChromeURLRequestContextGetter*>::iterator it =
      std::find(url_request_context_getters_.begin(),
                url_request_context_getters_.end(),
                url_request_context_getter);
  DCHECK(it != url_request_context_getters_.end());
  // This does not scale, but we shouldn't have many URLRequestContextGetters in
  // the first place, so this should be fine.
  url_request_context_getters_.erase(it);
}

void IOThread::ChangedToOnTheRecord() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  message_loop()->PostTask(
      FROM_HERE,
      NewRunnableMethod(
          this,
          &IOThread::ChangedToOnTheRecordOnIOThread));
}

net::ProxyScriptFetcher* IOThread::CreateAndRegisterProxyScriptFetcher(
    URLRequestContext* url_request_context) {
  return new ManagedProxyScriptFetcher(url_request_context, this);
}

void IOThread::Init() {
#if !defined(OS_CHROMEOS)
  // TODO(evan): test and enable this on all platforms.
  // Though this thread is called the "IO" thread, it actually just routes
  // messages around; it shouldn't be allowed to perform any blocking disk I/O.
  base::ThreadRestrictions::SetIOAllowed(false);
#endif

  BrowserProcessSubThread::Init();

  DCHECK_EQ(MessageLoop::TYPE_IO, message_loop()->type());

#if defined(USE_NSS)
  net::SetMessageLoopForOCSP();
#endif  // defined(USE_NSS)

  DCHECK(!globals_);
  globals_ = new Globals;

  globals_->net_log.reset(new ChromeNetLog());

  // Add an observer that will emit network change events to the ChromeNetLog.
  // Assuming NetworkChangeNotifier dispatches in FIFO order, we should be
  // logging the network change before other IO thread consumers respond to it.
  network_change_observer_.reset(
      new LoggingNetworkChangeObserver(globals_->net_log.get()));

  globals_->host_resolver.reset(
      CreateGlobalHostResolver(globals_->net_log.get()));
  globals_->dnsrr_resolver.reset(new net::DnsRRResolver);
  globals_->http_auth_handler_factory.reset(CreateDefaultAuthHandlerFactory(
      globals_->host_resolver.get()));
}

void IOThread::CleanUp() {
  // Step 1: Kill all things that might be holding onto
  // URLRequest/URLRequestContexts.

#if defined(USE_NSS)
  net::ShutdownOCSP();
#endif  // defined(USE_NSS)

  // Destroy all URLRequests started by URLFetchers.
  URLFetcher::CancelAll();

  // Break any cycles between the ProxyScriptFetcher and URLRequestContext.
  for (ProxyScriptFetchers::const_iterator it = fetchers_.begin();
       it != fetchers_.end(); ++it) {
    (*it)->Cancel();
  }

  // If any child processes are still running, terminate them and
  // and delete the BrowserChildProcessHost instances to release whatever
  // IO thread only resources they are referencing.
  BrowserChildProcessHost::TerminateAll();

  std::list<ChromeURLRequestContextGetter*> url_request_context_getters;
  url_request_context_getters.swap(url_request_context_getters_);
  for (std::list<ChromeURLRequestContextGetter*>::iterator it =
       url_request_context_getters.begin();
       it != url_request_context_getters.end(); ++it) {
    ChromeURLRequestContextGetter* getter = *it;
    getter->ReleaseURLRequestContext();
  }

  // Step 2: Release objects that the URLRequestContext could have been pointing
  // to.

  // This must be reset before the ChromeNetLog is destroyed.
  network_change_observer_.reset();

  // Not initialized in Init().  May not be initialized.
  if (predictor_) {
    predictor_->Shutdown();

    // TODO(willchan): Stop reference counting Predictor.  It's owned by
    // IOThread now.
    predictor_->Release();
    predictor_ = NULL;
    chrome_browser_net::FreePredictorResources();
  }

  // Deletion will unregister this interceptor.
  delete speculative_interceptor_;
  speculative_interceptor_ = NULL;

  // TODO(eroman): hack for http://crbug.com/15513
  if (globals_->host_resolver->GetAsHostResolverImpl()) {
    globals_->host_resolver.get()->GetAsHostResolverImpl()->Shutdown();
  }

  // We will delete the NetLog as part of CleanUpAfterMessageLoopDestruction()
  // in case any of the message loop destruction observers try to access it.
  deferred_net_log_to_delete_.reset(globals_->net_log.release());

  delete globals_;
  globals_ = NULL;

  BrowserProcessSubThread::CleanUp();
}

void IOThread::CleanUpAfterMessageLoopDestruction() {
  // TODO(eroman): get rid of this special case for 39723. If we could instead
  // have a method that runs after the message loop destruction observers have
  // run, but before the message loop itself is destroyed, we could safely
  // combine the two cleanups.
  deferred_net_log_to_delete_.reset();

  // This will delete the |notification_service_|.  Make sure it's done after
  // anything else can reference it.
  BrowserProcessSubThread::CleanUpAfterMessageLoopDestruction();

  // URLRequest instances must NOT outlive the IO thread.
  //
  // To allow for URLRequests to be deleted from
  // MessageLoop::DestructionObserver this check has to happen after CleanUp
  // (which runs before DestructionObservers).
  base::debug::LeakTracker<URLRequest>::CheckForLeaks();
}

net::HttpAuthHandlerFactory* IOThread::CreateDefaultAuthHandlerFactory(
    net::HostResolver* resolver) {
  const CommandLine& command_line = *CommandLine::ForCurrentProcess();

  // Get the whitelist information from the command line, create an
  // HttpAuthFilterWhitelist, and attach it to the HttpAuthHandlerFactory.
  net::HttpAuthFilterWhitelist* auth_filter_default_credentials = NULL;
  if (command_line.HasSwitch(switches::kAuthServerWhitelist)) {
    auth_filter_default_credentials = new net::HttpAuthFilterWhitelist(
        command_line.GetSwitchValueASCII(switches::kAuthServerWhitelist));
  }
  net::HttpAuthFilterWhitelist* auth_filter_delegate = NULL;
  if (command_line.HasSwitch(switches::kAuthNegotiateDelegateWhitelist)) {
    auth_filter_delegate = new net::HttpAuthFilterWhitelist(
        command_line.GetSwitchValueASCII(
            switches::kAuthNegotiateDelegateWhitelist));
  }
  globals_->url_security_manager.reset(
      net::URLSecurityManager::Create(auth_filter_default_credentials,
                                      auth_filter_delegate));

  // Determine which schemes are supported.
  std::string csv_auth_schemes = "basic,digest,ntlm,negotiate";
  if (command_line.HasSwitch(switches::kAuthSchemes))
    csv_auth_schemes = StringToLowerASCII(
        command_line.GetSwitchValueASCII(switches::kAuthSchemes));
  std::vector<std::string> supported_schemes;
  base::SplitString(csv_auth_schemes, ',', &supported_schemes);

  return net::HttpAuthHandlerRegistryFactory::Create(
      supported_schemes,
      globals_->url_security_manager.get(),
      resolver,
      command_line.HasSwitch(switches::kDisableAuthNegotiateCnameLookup),
      command_line.HasSwitch(switches::kEnableAuthNegotiatePort));
}

void IOThread::InitNetworkPredictorOnIOThread(
    bool prefetching_enabled,
    base::TimeDelta max_dns_queue_delay,
    size_t max_speculative_parallel_resolves,
    const chrome_common_net::UrlList& startup_urls,
    ListValue* referral_list,
    bool preconnect_enabled) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  CHECK(!predictor_);

  chrome_browser_net::EnablePredictor(prefetching_enabled);

  predictor_ = new chrome_browser_net::Predictor(
      globals_->host_resolver.get(),
      max_dns_queue_delay,
      max_speculative_parallel_resolves,
      preconnect_enabled);
  predictor_->AddRef();

  // Speculative_interceptor_ is used to predict subresource usage.
  DCHECK(!speculative_interceptor_);
  speculative_interceptor_ = new chrome_browser_net::ConnectInterceptor;

  FinalizePredictorInitialization(predictor_, startup_urls, referral_list);
}

void IOThread::ChangedToOnTheRecordOnIOThread() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  if (predictor_) {
    // Destroy all evidence of our OTR session.
    predictor_->Predictor::DiscardAllResults();
  }

  // Clear the host cache to avoid showing entries from the OTR session
  // in about:net-internals.
  if (globals_->host_resolver->GetAsHostResolverImpl()) {
    net::HostCache* host_cache =
        globals_->host_resolver.get()->GetAsHostResolverImpl()->cache();
    if (host_cache)
      host_cache->clear();
  }
  // Clear all of the passively logged data.
  // TODO(eroman): this is a bit heavy handed, really all we need to do is
  //               clear the data pertaining to off the record context.
  globals_->net_log->passive_collector()->Clear();
}
