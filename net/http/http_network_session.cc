// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_network_session.h"

#include <utility>

#include "base/logging.h"
#include "base/stl_util-inl.h"
#include "base/string_util.h"
#include "net/http/http_auth_handler_factory.h"
#include "net/http/url_security_manager.h"
#include "net/spdy/spdy_session_pool.h"

namespace net {

namespace {

// Total limit of sockets.
int g_max_sockets = 256;

// Default to allow up to 6 connections per host. Experiment and tuning may
// try other values (greater than 0).  Too large may cause many problems, such
// as home routers blocking the connections!?!?  See http://crbug.com/12066.
int g_max_sockets_per_group = 6;

// The max number of sockets to allow per proxy server.  This applies both to
// http and SOCKS proxies.  See http://crbug.com/12066 and
// http://crbug.com/44501 for details about proxy server connection limits.
int g_max_sockets_per_proxy_server = 32;

uint16 g_fixed_http_port = 0;
uint16 g_fixed_https_port = 0;

}  // namespace

HttpNetworkSession::HttpNetworkSession(
    HostResolver* host_resolver,
    ProxyService* proxy_service,
    ClientSocketFactory* client_socket_factory,
    SSLConfigService* ssl_config_service,
    SpdySessionPool* spdy_session_pool,
    HttpAuthHandlerFactory* http_auth_handler_factory,
    HttpNetworkDelegate* network_delegate,
    NetLog* net_log)
    : tcp_pool_histograms_(new ClientSocketPoolHistograms("TCP")),
      tcp_for_http_proxy_pool_histograms_(
          new ClientSocketPoolHistograms("TCPforHTTPProxy")),
      http_proxy_pool_histograms_(new ClientSocketPoolHistograms("HTTPProxy")),
      tcp_for_socks_pool_histograms_(
          new ClientSocketPoolHistograms("TCPforSOCKS")),
      socks_pool_histograms_(new ClientSocketPoolHistograms("SOCK")),
      ssl_pool_histograms_(new ClientSocketPoolHistograms("SSL")),
      tcp_socket_pool_(new TCPClientSocketPool(
          g_max_sockets, g_max_sockets_per_group, tcp_pool_histograms_,
          host_resolver, client_socket_factory, net_log)),
      ssl_socket_pool_(new SSLClientSocketPool(
          g_max_sockets, g_max_sockets_per_group, ssl_pool_histograms_,
          host_resolver, client_socket_factory, tcp_socket_pool_, NULL,
          NULL, net_log)),
      socket_factory_(client_socket_factory),
      host_resolver_(host_resolver),
      proxy_service_(proxy_service),
      ssl_config_service_(ssl_config_service),
      spdy_session_pool_(spdy_session_pool),
      http_auth_handler_factory_(http_auth_handler_factory),
      network_delegate_(network_delegate),
      net_log_(net_log) {
  DCHECK(proxy_service);
  DCHECK(ssl_config_service);
}

HttpNetworkSession::~HttpNetworkSession() {
}

const scoped_refptr<HttpProxyClientSocketPool>&
HttpNetworkSession::GetSocketPoolForHTTPProxy(const HostPortPair& http_proxy) {
  HTTPProxySocketPoolMap::const_iterator it =
      http_proxy_socket_pools_.find(http_proxy);
  if (it != http_proxy_socket_pools_.end())
    return it->second;

  std::pair<HTTPProxySocketPoolMap::iterator, bool> ret =
      http_proxy_socket_pools_.insert(
          std::make_pair(
              http_proxy,
              new HttpProxyClientSocketPool(
                  g_max_sockets_per_proxy_server, g_max_sockets_per_group,
                  http_proxy_pool_histograms_, host_resolver_,
                  new TCPClientSocketPool(
                      g_max_sockets_per_proxy_server, g_max_sockets_per_group,
                      tcp_for_http_proxy_pool_histograms_, host_resolver_,
                      socket_factory_, net_log_),
                  net_log_)));

  return ret.first->second;
}

const scoped_refptr<SOCKSClientSocketPool>&
HttpNetworkSession::GetSocketPoolForSOCKSProxy(
    const HostPortPair& socks_proxy) {
  SOCKSSocketPoolMap::const_iterator it = socks_socket_pools_.find(socks_proxy);
  if (it != socks_socket_pools_.end())
    return it->second;

  std::pair<SOCKSSocketPoolMap::iterator, bool> ret =
      socks_socket_pools_.insert(
          std::make_pair(socks_proxy, new SOCKSClientSocketPool(
              g_max_sockets_per_proxy_server, g_max_sockets_per_group,
              socks_pool_histograms_, host_resolver_,
              new TCPClientSocketPool(g_max_sockets_per_proxy_server,
                  g_max_sockets_per_group, tcp_for_socks_pool_histograms_,
                  host_resolver_, socket_factory_, net_log_),
              net_log_)));

  return ret.first->second;
}

const scoped_refptr<SSLClientSocketPool>&
HttpNetworkSession::GetSocketPoolForSSLWithProxy(
    const HostPortPair& proxy_server) {
  SSLSocketPoolMap::const_iterator it =
      ssl_socket_pools_for_proxies_.find(proxy_server);
  if (it != ssl_socket_pools_for_proxies_.end())
    return it->second;

  SSLClientSocketPool* new_pool = new SSLClientSocketPool(
      g_max_sockets_per_proxy_server, g_max_sockets_per_group,
      ssl_pool_histograms_, host_resolver_, socket_factory_,
      NULL,
      GetSocketPoolForHTTPProxy(proxy_server),
      GetSocketPoolForSOCKSProxy(proxy_server),
      net_log_);

  std::pair<SSLSocketPoolMap::iterator, bool> ret =
      ssl_socket_pools_for_proxies_.insert(std::make_pair(proxy_server,
                                                          new_pool));

  return ret.first->second;
}

// static
void HttpNetworkSession::set_max_sockets_per_group(int socket_count) {
  DCHECK_LT(0, socket_count);
  // The following is a sanity check... but we should NEVER be near this value.
  DCHECK_GT(100, socket_count);
  g_max_sockets_per_group = socket_count;
}

// static
uint16 HttpNetworkSession::fixed_http_port() {
  return g_fixed_http_port;
}

// static
void HttpNetworkSession::set_fixed_http_port(uint16 port) {
  g_fixed_http_port = port;
}

// static
uint16 HttpNetworkSession::fixed_https_port() {
  return g_fixed_https_port;
}

// static
void HttpNetworkSession::set_fixed_https_port(uint16 port) {
  g_fixed_https_port = port;
}

}  //  namespace net
