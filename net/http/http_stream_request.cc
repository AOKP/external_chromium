// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_stream_request.h"

#include "base/stl_util-inl.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/stringprintf.h"
#include "net/base/connection_type_histograms.h"
#include "net/base/net_log.h"
#include "net/base/net_util.h"
#include "net/base/ssl_cert_request_info.h"
#include "net/http/http_basic_stream.h"
#include "net/http/http_network_session.h"
#include "net/http/http_proxy_client_socket.h"
#include "net/http/http_proxy_client_socket_pool.h"
#include "net/http/http_request_info.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/client_socket_pool.h"
#include "net/socket/socks_client_socket_pool.h"
#include "net/socket/ssl_client_socket.h"
#include "net/socket/ssl_client_socket.h"
#include "net/socket/ssl_client_socket_pool.h"
#include "net/socket/tcp_client_socket_pool.h"
#include "net/spdy/spdy_http_stream.h"
#include "net/spdy/spdy_session.h"
#include "net/spdy/spdy_session_pool.h"

namespace net {

namespace {

GURL UpgradeUrlToHttps(const GURL& original_url) {
  GURL::Replacements replacements;
  // new_sheme and new_port need to be in scope here because GURL::Replacements
  // references the memory contained by them directly.
  const std::string new_scheme = "https";
  const std::string new_port = base::IntToString(443);
  replacements.SetSchemeStr(new_scheme);
  replacements.SetPortStr(new_port);
  return original_url.ReplaceComponents(replacements);
}

}  // namespace

HttpStreamRequest::HttpStreamRequest(
    StreamFactory* factory,
    HttpNetworkSession* session)
    : request_info_(NULL),
      proxy_info_(NULL),
      ssl_config_(NULL),
      session_(session),
      ALLOW_THIS_IN_INITIALIZER_LIST(
          io_callback_(this, &HttpStreamRequest::OnIOComplete)),
      connection_(new ClientSocketHandle),
      factory_(factory),
      delegate_(NULL),
      next_state_(STATE_NONE),
      pac_request_(NULL),
      using_ssl_(false),
      using_spdy_(false),
      force_spdy_always_(HttpStreamFactory::force_spdy_always()),
      force_spdy_over_ssl_(HttpStreamFactory::force_spdy_over_ssl()),
      spdy_certificate_error_(OK),
      establishing_tunnel_(false),
      was_alternate_protocol_available_(false),
      was_npn_negotiated_(false),
      preconnect_delegate_(NULL),
      num_streams_(0),
      ALLOW_THIS_IN_INITIALIZER_LIST(method_factory_(this)) {
  if (HttpStreamFactory::use_alternate_protocols())
    alternate_protocol_mode_ = kUnspecified;
  else
    alternate_protocol_mode_ = kDoNotUseAlternateProtocol;
}

HttpStreamRequest::~HttpStreamRequest() {
  // When we're in a partially constructed state, waiting for the user to
  // provide certificate handling information or authentication, we can't reuse
  // this stream at all.
  if (next_state_ == STATE_WAITING_USER_ACTION) {
    connection_->socket()->Disconnect();
    connection_.reset();
  }

  if (pac_request_)
    session_->proxy_service()->CancelPacRequest(pac_request_);

  // The stream could be in a partial state.  It is not reusable.
  if (stream_.get() && next_state_ != STATE_DONE)
    stream_->Close(true /* not reusable */);
}

void HttpStreamRequest::Start(const HttpRequestInfo* request_info,
                              SSLConfig* ssl_config,
                              ProxyInfo* proxy_info,
                              Delegate* delegate,
                              const BoundNetLog& net_log) {
  DCHECK(preconnect_delegate_ == NULL && delegate_ == NULL);
  DCHECK(delegate);
  delegate_ = delegate;
  StartInternal(request_info, ssl_config, proxy_info, net_log);
}

int HttpStreamRequest::Preconnect(int num_streams,
                                  const HttpRequestInfo* request_info,
                                  SSLConfig* ssl_config,
                                  ProxyInfo* proxy_info,
                                  PreconnectDelegate* delegate,
                                  const BoundNetLog& net_log) {
  DCHECK(preconnect_delegate_ == NULL && delegate_ == NULL);
  DCHECK(delegate);
  num_streams_ = num_streams;
  preconnect_delegate_ = delegate;
  return StartInternal(request_info, ssl_config, proxy_info, net_log);
}

int HttpStreamRequest::RestartWithCertificate(X509Certificate* client_cert) {
  ssl_config()->client_cert = client_cert;
  ssl_config()->send_client_cert = true;
  next_state_ = STATE_INIT_CONNECTION;
  // Reset the other member variables.
  // Note: this is necessary only with SSL renegotiation.
  stream_.reset();
  return RunLoop(OK);
}

int HttpStreamRequest::RestartTunnelWithProxyAuth(const string16& username,
                                                  const string16& password) {
  DCHECK(establishing_tunnel_);
  next_state_ = STATE_RESTART_TUNNEL_AUTH;
  stream_.reset();
  return RunLoop(OK);
}

LoadState HttpStreamRequest::GetLoadState() const {
  switch (next_state_) {
    case STATE_RESOLVE_PROXY_COMPLETE:
      return LOAD_STATE_RESOLVING_PROXY_FOR_URL;
    case STATE_CREATE_STREAM_COMPLETE:
      return connection_->GetLoadState();
    case STATE_INIT_CONNECTION_COMPLETE:
      return LOAD_STATE_SENDING_REQUEST;
    default:
      return LOAD_STATE_IDLE;
  }
}

void HttpStreamRequest::GetSSLInfo() {
  DCHECK(using_ssl_);
  DCHECK(!establishing_tunnel_);
  DCHECK(connection_.get() && connection_->socket());
  SSLClientSocket* ssl_socket =
      static_cast<SSLClientSocket*>(connection_->socket());
  ssl_socket->GetSSLInfo(&ssl_info_);
}

const HttpRequestInfo& HttpStreamRequest::request_info() const {
  return *request_info_;
}

ProxyInfo* HttpStreamRequest::proxy_info() const {
  return proxy_info_;
}

SSLConfig* HttpStreamRequest::ssl_config() const {
  return ssl_config_;
}

void HttpStreamRequest::OnStreamReadyCallback() {
  DCHECK(stream_.get());
  delegate_->OnStreamReady(stream_.release());
}

void HttpStreamRequest::OnStreamFailedCallback(int result) {
  delegate_->OnStreamFailed(result);
}

void HttpStreamRequest::OnCertificateErrorCallback(int result,
                                                   const SSLInfo& ssl_info) {
  delegate_->OnCertificateError(result, ssl_info);
}

void HttpStreamRequest::OnNeedsProxyAuthCallback(
    const HttpResponseInfo& response,
    HttpAuthController* auth_controller) {
  delegate_->OnNeedsProxyAuth(response, auth_controller);
}

void HttpStreamRequest::OnNeedsClientAuthCallback(
    SSLCertRequestInfo* cert_info) {
  delegate_->OnNeedsClientAuth(cert_info);
}

void HttpStreamRequest::OnPreconnectsComplete(int result) {
  preconnect_delegate_->OnPreconnectsComplete(this, result);
}

void HttpStreamRequest::OnIOComplete(int result) {
  RunLoop(result);
}

int HttpStreamRequest::RunLoop(int result) {
  result = DoLoop(result);

  DCHECK(delegate_ || preconnect_delegate_);

  if (result == ERR_IO_PENDING)
    return result;

  if (preconnect_delegate_) {
    MessageLoop::current()->PostTask(
        FROM_HERE,
        method_factory_.NewRunnableMethod(
            &HttpStreamRequest::OnPreconnectsComplete, result));
    return ERR_IO_PENDING;
  }

  if (IsCertificateError(result)) {
    // Retrieve SSL information from the socket.
    GetSSLInfo();

    next_state_ = STATE_WAITING_USER_ACTION;
    MessageLoop::current()->PostTask(
        FROM_HERE,
        method_factory_.NewRunnableMethod(
            &HttpStreamRequest::OnCertificateErrorCallback,
            result, ssl_info_));
    return ERR_IO_PENDING;
  }

  switch (result) {
    case ERR_PROXY_AUTH_REQUESTED:
      {
        DCHECK(connection_.get());
        DCHECK(connection_->socket());
        DCHECK(establishing_tunnel_);

        HttpProxyClientSocket* http_proxy_socket =
            static_cast<HttpProxyClientSocket*>(connection_->socket());
        const HttpResponseInfo* tunnel_auth_response =
            http_proxy_socket->GetResponseInfo();

        next_state_ = STATE_WAITING_USER_ACTION;
        MessageLoop::current()->PostTask(
            FROM_HERE,
            method_factory_.NewRunnableMethod(
                &HttpStreamRequest::OnNeedsProxyAuthCallback,
                *tunnel_auth_response,
                http_proxy_socket->auth_controller()));
      }
      return ERR_IO_PENDING;

    case ERR_SSL_CLIENT_AUTH_CERT_NEEDED:
      MessageLoop::current()->PostTask(
          FROM_HERE,
          method_factory_.NewRunnableMethod(
              &HttpStreamRequest::OnNeedsClientAuthCallback,
              connection_->ssl_error_response_info().cert_request_info));
      return ERR_IO_PENDING;

    case OK:
      next_state_ = STATE_DONE;
      MessageLoop::current()->PostTask(
          FROM_HERE,
          method_factory_.NewRunnableMethod(
              &HttpStreamRequest::OnStreamReadyCallback));
      return ERR_IO_PENDING;

    default:
      MessageLoop::current()->PostTask(
          FROM_HERE,
          method_factory_.NewRunnableMethod(
              &HttpStreamRequest::OnStreamFailedCallback,
              result));
      return ERR_IO_PENDING;
  }
  return result;
}

int HttpStreamRequest::DoLoop(int result) {
  DCHECK_NE(next_state_, STATE_NONE);
  int rv = result;
  do {
    State state = next_state_;
    next_state_ = STATE_NONE;
    switch (state) {
      case STATE_RESOLVE_PROXY:
        DCHECK_EQ(OK, rv);
        rv = DoResolveProxy();
        break;
      case STATE_RESOLVE_PROXY_COMPLETE:
        rv = DoResolveProxyComplete(rv);
        break;
      case STATE_INIT_CONNECTION:
        DCHECK_EQ(OK, rv);
        rv = DoInitConnection();
        break;
      case STATE_INIT_CONNECTION_COMPLETE:
        rv = DoInitConnectionComplete(rv);
        break;
      case STATE_WAITING_USER_ACTION:
        rv = DoWaitingUserAction(rv);
        break;
      case STATE_RESTART_TUNNEL_AUTH:
        DCHECK_EQ(OK, rv);
        rv = DoRestartTunnelAuth();
        break;
      case STATE_RESTART_TUNNEL_AUTH_COMPLETE:
        rv = DoRestartTunnelAuthComplete(rv);
        break;
      case STATE_CREATE_STREAM:
        DCHECK_EQ(OK, rv);
        rv = DoCreateStream();
        break;
      case STATE_CREATE_STREAM_COMPLETE:
        rv = DoCreateStreamComplete(rv);
        break;
      default:
        NOTREACHED() << "bad state";
        rv = ERR_FAILED;
        break;
    }
  } while (rv != ERR_IO_PENDING && next_state_ != STATE_NONE);
  return rv;
}

int HttpStreamRequest::StartInternal(const HttpRequestInfo* request_info,
                                     SSLConfig* ssl_config,
                                     ProxyInfo* proxy_info,
                                     const BoundNetLog& net_log) {
  CHECK_EQ(STATE_NONE, next_state_);
  request_info_ = request_info;
  ssl_config_ = ssl_config;
  proxy_info_ = proxy_info;
  net_log_ = net_log;
  next_state_ = STATE_RESOLVE_PROXY;
  int rv = RunLoop(OK);
  DCHECK_EQ(ERR_IO_PENDING, rv);
  return rv;
}

int HttpStreamRequest::DoResolveProxy() {
  DCHECK(!pac_request_);

  next_state_ = STATE_RESOLVE_PROXY_COMPLETE;

  // |endpoint_| indicates the final destination endpoint.
  endpoint_ = HostPortPair(request_info().url.HostNoBrackets(),
                           request_info().url.EffectiveIntPort());

  // Extra URL we might be attempting to resolve to.
  GURL alternate_endpoint_url = request_info().url;

  // Tracks whether we are using |request_.url| or |alternate_endpoint_url|.
  const GURL *curr_endpoint_url = &request_info().url;

  alternate_endpoint_url =
      factory_->ApplyHostMappingRules(alternate_endpoint_url, &endpoint_);

  const HttpAlternateProtocols& alternate_protocols =
      session_->alternate_protocols();
  if (HttpStreamFactory::spdy_enabled() &&
      alternate_protocols.HasAlternateProtocolFor(endpoint_)) {
    was_alternate_protocol_available_ = true;
    if (alternate_protocol_mode_ == kUnspecified) {
      HttpAlternateProtocols::PortProtocolPair alternate =
          alternate_protocols.GetAlternateProtocolFor(endpoint_);
      if (alternate.protocol != HttpAlternateProtocols::BROKEN) {
        DCHECK_LE(HttpAlternateProtocols::NPN_SPDY_1, alternate.protocol);
        DCHECK_GT(HttpAlternateProtocols::NUM_ALTERNATE_PROTOCOLS,
                  alternate.protocol);
        endpoint_.set_port(alternate.port);
        alternate_protocol_ = alternate.protocol;
        alternate_protocol_mode_ = kUsingAlternateProtocol;
        alternate_endpoint_url = UpgradeUrlToHttps(*curr_endpoint_url);
        curr_endpoint_url = &alternate_endpoint_url;
      }
    }
  }

  if (request_info().load_flags & LOAD_BYPASS_PROXY) {
    proxy_info()->UseDirect();
    return OK;
  }

  return session_->proxy_service()->ResolveProxy(
      *curr_endpoint_url, proxy_info(), &io_callback_, &pac_request_,
      net_log_);
}

int HttpStreamRequest::DoResolveProxyComplete(int result) {
  pac_request_ = NULL;

  if (result != OK)
    return result;

  // TODO(mbelshe): consider retrying ResolveProxy if we came here via use of
  // AlternateProtocol.

  // Remove unsupported proxies from the list.
  proxy_info()->RemoveProxiesWithoutScheme(
      ProxyServer::SCHEME_DIRECT |
      ProxyServer::SCHEME_HTTP | ProxyServer::SCHEME_HTTPS |
      ProxyServer::SCHEME_SOCKS4 | ProxyServer::SCHEME_SOCKS5);

  if (proxy_info()->is_empty()) {
    // No proxies/direct to choose from. This happens when we don't support any
    // of the proxies in the returned list.
    return ERR_NO_SUPPORTED_PROXIES;
  }

  next_state_ = STATE_INIT_CONNECTION;
  return OK;
}

int HttpStreamRequest::DoInitConnection() {
  DCHECK(!connection_->is_initialized());
  DCHECK(proxy_info()->proxy_server().is_valid());
  next_state_ = STATE_INIT_CONNECTION_COMPLETE;

  bool want_spdy_over_npn =
      alternate_protocol_mode_ == kUsingAlternateProtocol &&
      alternate_protocol_ == HttpAlternateProtocols::NPN_SPDY_2;
  using_ssl_ = request_info().url.SchemeIs("https") ||
      (force_spdy_always_ && force_spdy_over_ssl_) ||
      want_spdy_over_npn;
  using_spdy_ = false;

  // If spdy has been turned off on-the-fly, then there may be SpdySessions
  // still active.  But don't use them unless spdy is currently on.
  if (HttpStreamFactory::spdy_enabled()) {
    // Check first if we have a spdy session for this group.  If so, then go
    // straight to using that.
    HostPortProxyPair pair(endpoint_, proxy_info()->proxy_server());
    if (!preconnect_delegate_ &&
        session_->spdy_session_pool()->HasSession(pair)) {
      using_spdy_ = true;
      next_state_ = STATE_CREATE_STREAM;
      return OK;
    }
    // Check next if we have a spdy session for this proxy.  If so, then go
    // straight to using that.
    if (IsHttpsProxyAndHttpUrl()) {
      HostPortProxyPair proxy(proxy_info()->proxy_server().host_port_pair(),
                              ProxyServer::Direct());
      if (session_->spdy_session_pool()->HasSession(proxy)) {
        using_spdy_ = true;
        next_state_ = STATE_CREATE_STREAM;
        return OK;
      }
    }
  }

  // Build the string used to uniquely identify connections of this type.
  // Determine the host and port to connect to.
  std::string connection_group = endpoint_.ToString();
  DCHECK(!connection_group.empty());

  if (using_ssl_)
    connection_group = base::StringPrintf("ssl/%s", connection_group.c_str());

  // If the user is refreshing the page, bypass the host cache.
  bool disable_resolver_cache =
      request_info().load_flags & LOAD_BYPASS_CACHE ||
      request_info().load_flags & LOAD_VALIDATE_CACHE ||
      request_info().load_flags & LOAD_DISABLE_CACHE;

  // Build up the connection parameters.
  scoped_refptr<TCPSocketParams> tcp_params;
  scoped_refptr<HttpProxySocketParams> http_proxy_params;
  scoped_refptr<SOCKSSocketParams> socks_params;
  scoped_ptr<HostPortPair> proxy_host_port;

  if (proxy_info()->is_direct()) {
    tcp_params = new TCPSocketParams(endpoint_, request_info().priority,
                                     request_info().referrer,
                                     disable_resolver_cache);
  } else {
    ProxyServer proxy_server = proxy_info()->proxy_server();
    proxy_host_port.reset(new HostPortPair(proxy_server.host_port_pair()));
    scoped_refptr<TCPSocketParams> proxy_tcp_params =
        new TCPSocketParams(*proxy_host_port, request_info().priority,
                            request_info().referrer, disable_resolver_cache);

    if (proxy_info()->is_http() || proxy_info()->is_https()) {
      GURL authentication_url = request_info().url;
      if (using_ssl_ && !authentication_url.SchemeIs("https")) {
        // If a proxy tunnel connection needs to be established due to
        // an Alternate-Protocol, the URL needs to be changed to indicate
        // https or digest authentication attempts will fail.
        // For example, suppose the initial request was for
        // "http://www.example.com/index.html". If this is an SSL
        // upgrade due to alternate protocol, the digest authorization
        // should have a uri="www.example.com:443" field rather than a
        // "/index.html" entry, even though the original request URL has not
        // changed.
        authentication_url = UpgradeUrlToHttps(authentication_url);
      }
      establishing_tunnel_ = using_ssl_;
      std::string user_agent;
      request_info().extra_headers.GetHeader(HttpRequestHeaders::kUserAgent,
                                             &user_agent);
      scoped_refptr<SSLSocketParams> ssl_params;
      if (proxy_info()->is_https()) {
        // Set ssl_params, and unset proxy_tcp_params
        ssl_params = GenerateSslParams(proxy_tcp_params, NULL, NULL,
                                       ProxyServer::SCHEME_DIRECT,
                                       proxy_host_port->host(),
                                       want_spdy_over_npn);
        proxy_tcp_params = NULL;
      }

      http_proxy_params =
          new HttpProxySocketParams(proxy_tcp_params,
                                    ssl_params,
                                    authentication_url,
                                    user_agent,
                                    endpoint_,
                                    session_->auth_cache(),
                                    session_->http_auth_handler_factory(),
                                    session_->spdy_session_pool(),
                                    session_->mutable_spdy_settings(),
                                    using_ssl_);
    } else {
      DCHECK(proxy_info()->is_socks());
      char socks_version;
      if (proxy_server.scheme() == ProxyServer::SCHEME_SOCKS5)
        socks_version = '5';
      else
        socks_version = '4';
      connection_group = base::StringPrintf(
          "socks%c/%s", socks_version, connection_group.c_str());

      socks_params = new SOCKSSocketParams(proxy_tcp_params,
                                           socks_version == '5',
                                           endpoint_,
                                           request_info().priority,
                                           request_info().referrer);
    }
  }

  // Deal with SSL - which layers on top of any given proxy.
  if (using_ssl_) {
    scoped_refptr<SSLSocketParams> ssl_params =
        GenerateSslParams(tcp_params, http_proxy_params, socks_params,
                          proxy_info()->proxy_server().scheme(),
                          request_info().url.HostNoBrackets(),
                          want_spdy_over_npn);
    SSLClientSocketPool* ssl_pool = NULL;
    if (proxy_info()->is_direct())
      ssl_pool = session_->ssl_socket_pool();
    else
      ssl_pool = session_->GetSocketPoolForSSLWithProxy(*proxy_host_port);

    if (preconnect_delegate_) {
      RequestSocketsForPool(ssl_pool, connection_group, ssl_params,
                            num_streams_, net_log_);
      return OK;
    }

    return connection_->Init(connection_group, ssl_params,
                             request_info().priority, &io_callback_, ssl_pool,
                             net_log_);
  }

  // Finally, get the connection started.
  if (proxy_info()->is_http() || proxy_info()->is_https()) {
    HttpProxyClientSocketPool* pool =
        session_->GetSocketPoolForHTTPProxy(*proxy_host_port);
    if (preconnect_delegate_) {
      RequestSocketsForPool(pool, connection_group, http_proxy_params,
                            num_streams_, net_log_);
      return OK;
    }

    return connection_->Init(connection_group, http_proxy_params,
                             request_info().priority, &io_callback_,
                             pool, net_log_);
  }

  if (proxy_info()->is_socks()) {
    SOCKSClientSocketPool* pool =
        session_->GetSocketPoolForSOCKSProxy(*proxy_host_port);
    if (preconnect_delegate_) {
      RequestSocketsForPool(pool, connection_group, socks_params,
                            num_streams_, net_log_);
      return OK;
    }

    return connection_->Init(connection_group, socks_params,
                             request_info().priority, &io_callback_, pool,
                             net_log_);
  }

  DCHECK(proxy_info()->is_direct());

  TCPClientSocketPool* pool = session_->tcp_socket_pool();
  if (preconnect_delegate_) {
    RequestSocketsForPool(pool, connection_group, tcp_params,
                          num_streams_, net_log_);
    return OK;
  }

  return connection_->Init(connection_group, tcp_params,
                           request_info().priority, &io_callback_,
                           pool, net_log_);
}

int HttpStreamRequest::DoInitConnectionComplete(int result) {
  if (preconnect_delegate_) {
    DCHECK_EQ(OK, result);
    return OK;
  }

  // |result| may be the result of any of the stacked pools. The following
  // logic is used when determining how to interpret an error.
  // If |result| < 0:
  //   and connection_->socket() != NULL, then the SSL handshake ran and it
  //     is a potentially recoverable error.
  //   and connection_->socket == NULL and connection_->is_ssl_error() is true,
  //     then the SSL handshake ran with an unrecoverable error.
  //   otherwise, the error came from one of the other pools.
  bool ssl_started = using_ssl_ && (result == OK || connection_->socket() ||
                                    connection_->is_ssl_error());

  if (ssl_started && (result == OK || IsCertificateError(result))) {
    SSLClientSocket* ssl_socket =
      static_cast<SSLClientSocket*>(connection_->socket());
    if (ssl_socket->was_npn_negotiated()) {
      was_npn_negotiated_ = true;
      if (ssl_socket->was_spdy_negotiated())
        SwitchToSpdyMode();
    }
    if (force_spdy_over_ssl_ && force_spdy_always_)
      SwitchToSpdyMode();
  } else if (proxy_info()->is_https() && connection_->socket() &&
        result == OK) {
    HttpProxyClientSocket* proxy_socket =
      static_cast<HttpProxyClientSocket*>(connection_->socket());
    if (proxy_socket->using_spdy()) {
      was_npn_negotiated_ = true;
      SwitchToSpdyMode();
    }
  }

  // We may be using spdy without SSL
  if (!force_spdy_over_ssl_ && force_spdy_always_)
    SwitchToSpdyMode();

  if (result == ERR_PROXY_AUTH_REQUESTED) {
    DCHECK(!ssl_started);
    // Other state (i.e. |using_ssl_|) suggests that |connection_| will have an
    // SSL socket, but there was an error before that could happen.  This
    // puts the in progress HttpProxy socket into |connection_| in order to
    // complete the auth.  The tunnel restart code is careful to remove it
    // before returning control to the rest of this class.
    connection_.reset(connection_->release_pending_http_proxy_connection());
    return result;
  }

  if ((!ssl_started && result < 0 &&
       alternate_protocol_mode_ == kUsingAlternateProtocol) ||
      result == ERR_NPN_NEGOTIATION_FAILED) {
    // Mark the alternate protocol as broken and fallback.
    MarkBrokenAlternateProtocolAndFallback();
    return OK;
  }

  if (result < 0 && !ssl_started)
    return ReconsiderProxyAfterError(result);
  establishing_tunnel_ = false;

  if (connection_->socket()) {
    LogHttpConnectedMetrics(*connection_);

    // We officially have a new connection.  Record the type.
    if (!connection_->is_reused()) {
      ConnectionType type = using_spdy_ ? CONNECTION_SPDY : CONNECTION_HTTP;
      UpdateConnectionTypeHistograms(type);
    }
  }

  // Handle SSL errors below.
  if (using_ssl_) {
    DCHECK(ssl_started);
    if (IsCertificateError(result)) {
      if (using_spdy_ && request_info().url.SchemeIs("http")) {
        // We ignore certificate errors for http over spdy.
        spdy_certificate_error_ = result;
        result = OK;
      } else {
        result = HandleCertificateError(result);
        if (result == OK && !connection_->socket()->IsConnectedAndIdle()) {
          connection_->socket()->Disconnect();
          connection_->Reset();
          next_state_ = STATE_INIT_CONNECTION;
          return result;
        }
      }
    }
    if (result < 0)
      return HandleSSLHandshakeError(result);
  }

  next_state_ = STATE_CREATE_STREAM;
  return OK;
}

int HttpStreamRequest::DoWaitingUserAction(int result) {
  // This state indicates that the stream request is in a partially
  // completed state, and we've called back to the delegate for more
  // information.

  // We're always waiting here for the delegate to call us back.
  return ERR_IO_PENDING;
}

int HttpStreamRequest::DoCreateStream() {
  next_state_ = STATE_CREATE_STREAM_COMPLETE;

  // We only set the socket motivation if we're the first to use
  // this socket.  Is there a race for two SPDY requests?  We really
  // need to plumb this through to the connect level.
  if (connection_->socket() && !connection_->is_reused())
    SetSocketMotivation();

  if (!using_spdy_) {
    stream_.reset(new HttpBasicStream(connection_.release()));
    return OK;
  }

  CHECK(!stream_.get());

  bool direct = true;
  SpdySessionPool* spdy_pool = session_->spdy_session_pool();
  scoped_refptr<SpdySession> spdy_session;

  const ProxyServer& proxy_server = proxy_info()->proxy_server();
  HostPortProxyPair pair(endpoint_, proxy_server);
  if (spdy_pool->HasSession(pair)) {
    // We have a SPDY session to the origin server.  This might be a direct
    // connection, or it might be a SPDY session through an HTTP or HTTPS proxy.
    spdy_session =
        spdy_pool->Get(pair, session_->mutable_spdy_settings(), net_log_);
  } else if (IsHttpsProxyAndHttpUrl()) {
    // If we don't have a direct SPDY session, and we're using an HTTPS
    // proxy, then we might have a SPDY session to the proxy
    pair = HostPortProxyPair(proxy_server.host_port_pair(),
                             ProxyServer::Direct());
    if (spdy_pool->HasSession(pair)) {
      spdy_session =
          spdy_pool->Get(pair, session_->mutable_spdy_settings(), net_log_);
    }
    direct = false;
  }

  if (!spdy_session.get()) {
    // SPDY can be negotiated using the TLS next protocol negotiation (NPN)
    // extension, or just directly using SSL. Either way, |connection_| must
    // contain an SSLClientSocket.
    CHECK(connection_->socket());
    int error = spdy_pool->GetSpdySessionFromSocket(
        pair, session_->mutable_spdy_settings(), connection_.release(),
        net_log_, spdy_certificate_error_, &spdy_session, using_ssl_);
    if (error != OK)
      return error;
  }

  if (spdy_session->IsClosed())
    return ERR_CONNECTION_CLOSED;

  bool useRelativeUrl = direct || request_info().url.SchemeIs("https");
  stream_.reset(new SpdyHttpStream(spdy_session, useRelativeUrl));
  return OK;
}

int HttpStreamRequest::DoCreateStreamComplete(int result) {
  if (result < 0)
    return result;

  next_state_ = STATE_NONE;
  return OK;
}

int HttpStreamRequest::DoRestartTunnelAuth() {
  next_state_ = STATE_RESTART_TUNNEL_AUTH_COMPLETE;
  HttpProxyClientSocket* http_proxy_socket =
      static_cast<HttpProxyClientSocket*>(connection_->socket());
  return http_proxy_socket->RestartWithAuth(&io_callback_);
}

int HttpStreamRequest::DoRestartTunnelAuthComplete(int result) {
  if (result == ERR_PROXY_AUTH_REQUESTED)
    return result;

  if (result == OK) {
    // Now that we've got the HttpProxyClientSocket connected.  We have
    // to release it as an idle socket into the pool and start the connection
    // process from the beginning.  Trying to pass it in with the
    // SSLSocketParams might cause a deadlock since params are dispatched
    // interchangeably.  This request won't necessarily get this http proxy
    // socket, but there will be forward progress.
    connection_->Reset();
    establishing_tunnel_ = false;
    next_state_ = STATE_INIT_CONNECTION;
    return OK;
  }

  return ReconsiderProxyAfterError(result);
}

void HttpStreamRequest::SetSocketMotivation() {
  if (request_info_->motivation == HttpRequestInfo::PRECONNECT_MOTIVATED)
    connection_->socket()->SetSubresourceSpeculation();
  else if (request_info_->motivation == HttpRequestInfo::OMNIBOX_MOTIVATED)
    connection_->socket()->SetOmniboxSpeculation();
  // TODO(mbelshe): Add other motivations (like EARLY_LOAD_MOTIVATED).
}

bool HttpStreamRequest::IsHttpsProxyAndHttpUrl() {
  return proxy_info()->is_https() && request_info().url.SchemeIs("http");
}

// Returns a newly create SSLSocketParams, and sets several
// fields of ssl_config_.
scoped_refptr<SSLSocketParams> HttpStreamRequest::GenerateSslParams(
    scoped_refptr<TCPSocketParams> tcp_params,
    scoped_refptr<HttpProxySocketParams> http_proxy_params,
    scoped_refptr<SOCKSSocketParams> socks_params,
    ProxyServer::Scheme proxy_scheme,
    std::string hostname,
    bool want_spdy_over_npn) {

  if (factory_->IsTLSIntolerantServer(request_info().url)) {
    LOG(WARNING) << "Falling back to SSLv3 because host is TLS intolerant: "
        << GetHostAndPort(request_info().url);
    ssl_config()->ssl3_fallback = true;
    ssl_config()->tls1_enabled = false;
  }

  UMA_HISTOGRAM_ENUMERATION("Net.ConnectionUsedSSLv3Fallback",
                            static_cast<int>(ssl_config()->ssl3_fallback), 2);

  int load_flags = request_info().load_flags;
  if (HttpStreamFactory::ignore_certificate_errors())
    load_flags |= LOAD_IGNORE_ALL_CERT_ERRORS;
  if (request_info().load_flags & LOAD_VERIFY_EV_CERT)
    ssl_config()->verify_ev_cert = true;

    if (proxy_info()->proxy_server().scheme() == ProxyServer::SCHEME_HTTP ||
        proxy_info()->proxy_server().scheme() == ProxyServer::SCHEME_HTTPS) {
      ssl_config()->mitm_proxies_allowed = true;
    }

  scoped_refptr<SSLSocketParams> ssl_params =
      new SSLSocketParams(tcp_params, socks_params, http_proxy_params,
                          proxy_scheme, hostname,
                          *ssl_config(), load_flags,
                          force_spdy_always_ && force_spdy_over_ssl_,
                          want_spdy_over_npn);

  return ssl_params;
}


void HttpStreamRequest::MarkBrokenAlternateProtocolAndFallback() {
  // We have to:
  // * Reset the endpoint to be the unmodified URL specified destination.
  // * Mark the endpoint as broken so we don't try again.
  // * Set the alternate protocol mode to kDoNotUseAlternateProtocol so we
  // ignore future Alternate-Protocol headers from the HostPortPair.
  // * Reset the connection and go back to STATE_INIT_CONNECTION.

  endpoint_ = HostPortPair(request_info().url.HostNoBrackets(),
                           request_info().url.EffectiveIntPort());

  session_->mutable_alternate_protocols()->MarkBrokenAlternateProtocolFor(
      endpoint_);

  alternate_protocol_mode_ = kDoNotUseAlternateProtocol;
  if (connection_->socket())
    connection_->socket()->Disconnect();
  connection_->Reset();
  next_state_ = STATE_INIT_CONNECTION;
}

int HttpStreamRequest::ReconsiderProxyAfterError(int error) {
  DCHECK(!pac_request_);

  // A failure to resolve the hostname or any error related to establishing a
  // TCP connection could be grounds for trying a new proxy configuration.
  //
  // Why do this when a hostname cannot be resolved?  Some URLs only make sense
  // to proxy servers.  The hostname in those URLs might fail to resolve if we
  // are still using a non-proxy config.  We need to check if a proxy config
  // now exists that corresponds to a proxy server that could load the URL.
  //
  switch (error) {
    case ERR_PROXY_CONNECTION_FAILED:
    case ERR_NAME_NOT_RESOLVED:
    case ERR_INTERNET_DISCONNECTED:
    case ERR_ADDRESS_UNREACHABLE:
    case ERR_CONNECTION_CLOSED:
    case ERR_CONNECTION_RESET:
    case ERR_CONNECTION_REFUSED:
    case ERR_CONNECTION_ABORTED:
    case ERR_TIMED_OUT:
    case ERR_TUNNEL_CONNECTION_FAILED:
    case ERR_SOCKS_CONNECTION_FAILED:
      break;
    case ERR_SOCKS_CONNECTION_HOST_UNREACHABLE:
      // Remap the SOCKS-specific "host unreachable" error to a more
      // generic error code (this way consumers like the link doctor
      // know to substitute their error page).
      //
      // Note that if the host resolving was done by the SOCSK5 proxy, we can't
      // differentiate between a proxy-side "host not found" versus a proxy-side
      // "address unreachable" error, and will report both of these failures as
      // ERR_ADDRESS_UNREACHABLE.
      return ERR_ADDRESS_UNREACHABLE;
    default:
      return error;
  }

  if (request_info().load_flags & LOAD_BYPASS_PROXY) {
    return error;
  }

  int rv = session_->proxy_service()->ReconsiderProxyAfterError(
      request_info().url, proxy_info(), &io_callback_, &pac_request_,
      net_log_);
  if (rv == OK || rv == ERR_IO_PENDING) {
    // If the error was during connection setup, there is no socket to
    // disconnect.
    if (connection_->socket())
      connection_->socket()->Disconnect();
    connection_->Reset();
    next_state_ = STATE_RESOLVE_PROXY_COMPLETE;
  } else {
    // If ReconsiderProxyAfterError() failed synchronously, it means
    // there was nothing left to fall-back to, so fail the transaction
    // with the last connection error we got.
    // TODO(eroman): This is a confusing contract, make it more obvious.
    rv = error;
  }

  return rv;
}

int HttpStreamRequest::HandleCertificateError(int error) {
  DCHECK(using_ssl_);
  DCHECK(IsCertificateError(error));

  SSLClientSocket* ssl_socket =
      static_cast<SSLClientSocket*>(connection_->socket());
  ssl_socket->GetSSLInfo(&ssl_info_);

  // Add the bad certificate to the set of allowed certificates in the
  // SSL info object. This data structure will be consulted after calling
  // RestartIgnoringLastError(). And the user will be asked interactively
  // before RestartIgnoringLastError() is ever called.
  SSLConfig::CertAndStatus bad_cert;
  bad_cert.cert = ssl_info_.cert;
  bad_cert.cert_status = ssl_info_.cert_status;
  ssl_config()->allowed_bad_certs.push_back(bad_cert);

  int load_flags = request_info().load_flags;
  if (HttpStreamFactory::ignore_certificate_errors())
    load_flags |= LOAD_IGNORE_ALL_CERT_ERRORS;
  if (ssl_socket->IgnoreCertError(error, load_flags))
    return OK;
  return error;
}

int HttpStreamRequest::HandleSSLHandshakeError(int error) {
  if (ssl_config()->send_client_cert &&
      (error == ERR_SSL_PROTOCOL_ERROR ||
       error == ERR_BAD_SSL_CLIENT_AUTH_CERT)) {
    session_->ssl_client_auth_cache()->Remove(
        GetHostAndPort(request_info().url));
  }

  switch (error) {
    case ERR_SSL_PROTOCOL_ERROR:
    case ERR_SSL_VERSION_OR_CIPHER_MISMATCH:
    case ERR_SSL_DECOMPRESSION_FAILURE_ALERT:
    case ERR_SSL_BAD_RECORD_MAC_ALERT:
      if (ssl_config()->tls1_enabled &&
          !SSLConfigService::IsKnownStrictTLSServer(
          request_info().url.host())) {
        // This could be a TLS-intolerant server, an SSL 3.0 server that
        // chose a TLS-only cipher suite or a server with buggy DEFLATE
        // support. Turn off TLS 1.0, DEFLATE support and retry.
        factory_->AddTLSIntolerantServer(request_info().url);
        next_state_ = STATE_INIT_CONNECTION;
        DCHECK(!connection_.get() || !connection_->socket());
        error = OK;
      }
      break;
  }
  return error;
}

void HttpStreamRequest::SwitchToSpdyMode() {
  if (HttpStreamFactory::spdy_enabled())
    using_spdy_ = true;
}

// static
void HttpStreamRequest::LogHttpConnectedMetrics(
    const ClientSocketHandle& handle) {
  UMA_HISTOGRAM_ENUMERATION("Net.HttpSocketType", handle.reuse_type(),
                            ClientSocketHandle::NUM_TYPES);

  switch (handle.reuse_type()) {
    case ClientSocketHandle::UNUSED:
      UMA_HISTOGRAM_CUSTOM_TIMES("Net.HttpConnectionLatency",
                                 handle.setup_time(),
                                 base::TimeDelta::FromMilliseconds(1),
                                 base::TimeDelta::FromMinutes(10),
                                 100);
      break;
    case ClientSocketHandle::UNUSED_IDLE:
      UMA_HISTOGRAM_CUSTOM_TIMES("Net.SocketIdleTimeBeforeNextUse_UnusedSocket",
                                 handle.idle_time(),
                                 base::TimeDelta::FromMilliseconds(1),
                                 base::TimeDelta::FromMinutes(6),
                                 100);
      break;
    case ClientSocketHandle::REUSED_IDLE:
      UMA_HISTOGRAM_CUSTOM_TIMES("Net.SocketIdleTimeBeforeNextUse_ReusedSocket",
                                 handle.idle_time(),
                                 base::TimeDelta::FromMilliseconds(1),
                                 base::TimeDelta::FromMinutes(6),
                                 100);
      break;
    default:
      NOTREACHED();
      break;
  }
}

}  // namespace net
