// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This class represents contextual information (cookies, cache, etc.)
// that's useful when processing resource requests.
// The class is reference-counted so that it can be cleaned up after any
// requests that are using it have been completed.

#ifndef NET_URL_REQUEST_URL_REQUEST_CONTEXT_H_
#define NET_URL_REQUEST_URL_REQUEST_CONTEXT_H_
#pragma once

#include "base/non_thread_safe.h"
#include "base/ref_counted.h"
#include "net/base/net_log.h"
#include "net/base/ssl_config_service.h"
#include "net/base/transport_security_state.h"
#include "net/ftp/ftp_auth_cache.h"
#include "net/proxy/proxy_service.h"
#include "net/socket/dns_cert_provenance_checker.h"

namespace net {
class CertVerifier;
class CookiePolicy;
class CookieStore;
class DnsCertProvenanceChecker;
class DnsRRResolver;
class FtpTransactionFactory;
class HostResolver;
class HttpAuthHandlerFactory;
class HttpNetworkDelegate;
class HttpTransactionFactory;
class SSLConfigService;
class URLRequest;
}  // namespace net

// Subclass to provide application-specific context for net::URLRequest
// instances.
class URLRequestContext
    : public base::RefCountedThreadSafe<URLRequestContext>,
      public NonThreadSafe {
 public:
  URLRequestContext();

  net::NetLog* net_log() const {
    return net_log_;
  }

  void set_net_log(net::NetLog* net_log) {
    net_log_ = net_log;
  }

  net::HostResolver* host_resolver() const {
    return host_resolver_;
  }

  void set_host_resolver(net::HostResolver* host_resolver) {
    host_resolver_ = host_resolver;
  }

  net::CertVerifier* cert_verifier() const {
    return cert_verifier_;
  }

  void set_cert_verifier(net::CertVerifier* cert_verifier) {
    cert_verifier_ = cert_verifier;
  }

  net::DnsRRResolver* dnsrr_resolver() const {
    return dnsrr_resolver_;
  }

  void set_dnsrr_resolver(net::DnsRRResolver* dnsrr_resolver) {
    dnsrr_resolver_ = dnsrr_resolver;
  }

  net::DnsCertProvenanceChecker* dns_cert_checker() const {
    return dns_cert_checker_.get();
  }

  // Get the proxy service for this context.
  net::ProxyService* proxy_service() const {
    return proxy_service_;
  }

  void set_proxy_service(net::ProxyService* proxy_service) {
    proxy_service_ = proxy_service;
  }

  // Get the ssl config service for this context.
  net::SSLConfigService* ssl_config_service() const {
    return ssl_config_service_;
  }

  // Gets the HTTP Authentication Handler Factory for this context.
  // The factory is only valid for the lifetime of this URLRequestContext
  net::HttpAuthHandlerFactory* http_auth_handler_factory() {
    return http_auth_handler_factory_;
  }
  void set_http_auth_handler_factory(net::HttpAuthHandlerFactory* factory) {
    http_auth_handler_factory_ = factory;
  }

  // Gets the http transaction factory for this context.
  net::HttpTransactionFactory* http_transaction_factory() const {
    return http_transaction_factory_;
  }

  void set_http_transaction_factory(net::HttpTransactionFactory* factory) {
    http_transaction_factory_ = factory;
  }

  // Gets the ftp transaction factory for this context.
  net::FtpTransactionFactory* ftp_transaction_factory() {
    return ftp_transaction_factory_;
  }

  // Gets the cookie store for this context (may be null, in which case
  // cookies are not stored).
  net::CookieStore* cookie_store() { return cookie_store_.get(); }

  void set_cookie_store(net::CookieStore* cookie_store);

  // Gets the cookie policy for this context (may be null, in which case
  // cookies are allowed).
  net::CookiePolicy* cookie_policy() { return cookie_policy_; }

  net::TransportSecurityState* transport_security_state() {
      return transport_security_state_; }

  // Gets the FTP authentication cache for this context.
  net::FtpAuthCache* ftp_auth_cache() { return &ftp_auth_cache_; }

  // Gets the value of 'Accept-Charset' header field.
  const std::string& accept_charset() const { return accept_charset_; }

#ifdef ANDROID
  // Gets the value of 'Accept-Language' header field.
  virtual const std::string& GetAcceptLanguage() const { return accept_language_; }
#else
  // Gets the value of 'Accept-Language' header field.
  const std::string& accept_language() const { return accept_language_; }
#endif

  // Gets the UA string to use for the given URL.  Pass an invalid URL (such as
  // GURL()) to get the default UA string.  Subclasses should override this
  // method to provide a UA string.
  virtual const std::string& GetUserAgent(const GURL& url) const;

  // In general, referrer_charset is not known when URLRequestContext is
  // constructed. So, we need a setter.
  const std::string& referrer_charset() const { return referrer_charset_; }
  void set_referrer_charset(const std::string& charset) {
    referrer_charset_ = charset;
  }

  // Controls whether or not the URLRequestContext considers itself to be the
  // "main" URLRequestContext.
  bool is_main() const { return is_main_; }
  void set_is_main(bool is_main) { is_main_ = is_main; }

 protected:
  friend class base::RefCountedThreadSafe<URLRequestContext>;

  virtual ~URLRequestContext();

  // The following members are expected to be initialized and owned by
  // subclasses.
  net::NetLog* net_log_;
  net::HostResolver* host_resolver_;
  net::CertVerifier* cert_verifier_;
  net::DnsRRResolver* dnsrr_resolver_;
  scoped_ptr<net::DnsCertProvenanceChecker> dns_cert_checker_;
  scoped_refptr<net::ProxyService> proxy_service_;
  scoped_refptr<net::SSLConfigService> ssl_config_service_;
  net::HttpTransactionFactory* http_transaction_factory_;
  net::FtpTransactionFactory* ftp_transaction_factory_;
  net::HttpAuthHandlerFactory* http_auth_handler_factory_;
  net::HttpNetworkDelegate* network_delegate_;
  scoped_refptr<net::CookieStore> cookie_store_;
  net::CookiePolicy* cookie_policy_;
  scoped_refptr<net::TransportSecurityState> transport_security_state_;
  net::FtpAuthCache ftp_auth_cache_;
  std::string accept_language_;
  std::string accept_charset_;
  // The charset of the referrer where this request comes from. It's not
  // used in communication with a server but is used to construct a suggested
  // filename for file download.
  std::string referrer_charset_;

 private:
  // Indicates whether or not this is the main URLRequestContext.
  bool is_main_;

  DISALLOW_COPY_AND_ASSIGN(URLRequestContext);
};

#endif  // NET_URL_REQUEST_URL_REQUEST_CONTEXT_H_
