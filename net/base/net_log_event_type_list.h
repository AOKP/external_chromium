// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// NOTE: No header guards are used, since this file is intended to be expanded
// directly into net_log.h. DO NOT include this file anywhere else.

// --------------------------------------------------------------------------
// General pseudo-events
// --------------------------------------------------------------------------

// Something got cancelled (we determine what is cancelled based on the
// log context around it.)
EVENT_TYPE(CANCELLED)

// Marks the creation/destruction of a request (URLRequest or SocketStream).
EVENT_TYPE(REQUEST_ALIVE)

// ------------------------------------------------------------------------
// HostResolverImpl
// ------------------------------------------------------------------------

// The start/end of a host resolve (DNS) request.
// If an error occurred, the end phase will contain these parameters:
//   {
//     "net_error": <The net error code integer for the failure>,
//     "os_error": <The exact error code integer that getaddrinfo() returned>,
//     "was_from_cache": <True if the response was gotten from the cache>
//   }
EVENT_TYPE(HOST_RESOLVER_IMPL)

// ------------------------------------------------------------------------
// InitProxyResolver
// ------------------------------------------------------------------------

// The start/end of auto-detect + custom PAC URL configuration.
EVENT_TYPE(INIT_PROXY_RESOLVER)

// The start/end of download of a PAC script. This could be the well-known
// WPAD URL (if testing auto-detect), or a custom PAC URL.
//
// The START event has the parameters:
//   {
//     "url": <URL string of script being fetched>
//   }
//
// If the fetch failed, then the END phase has these parameters:
//   {
//      "error_code": <Net error code integer>
//   }
EVENT_TYPE(INIT_PROXY_RESOLVER_FETCH_PAC_SCRIPT)

// The start/end of the testing of a PAC script (trying to parse the fetched
// file as javascript).
//
// If the parsing of the script failed, the END phase will have parameters:
//   {
//      "error_code": <Net error code integer>
//   }
EVENT_TYPE(INIT_PROXY_RESOLVER_SET_PAC_SCRIPT)

// This event means that initialization failed because there was no
// configured script fetcher. (This indicates a configuration error).
EVENT_TYPE(INIT_PROXY_RESOLVER_HAS_NO_FETCHER)

// This event is emitted after deciding to fall-back to the next PAC
// script in the list.
EVENT_TYPE(INIT_PROXY_RESOLVER_FALLING_BACK_TO_NEXT_PAC_URL)

// ------------------------------------------------------------------------
// ProxyService
// ------------------------------------------------------------------------

// The start/end of a proxy resolve request.
EVENT_TYPE(PROXY_SERVICE)

// The time while a request is waiting on InitProxyResolver to configure
// against either WPAD or custom PAC URL. The specifics on this time
// are found from ProxyService::init_proxy_resolver_log().
EVENT_TYPE(PROXY_SERVICE_WAITING_FOR_INIT_PAC)

// The time taken to fetch the system proxy configuration.
EVENT_TYPE(PROXY_SERVICE_POLL_CONFIG_SERVICE_FOR_CHANGES)

// This event is emitted to show what the PAC script returned. It can contain
// extra parameters that are either:
//   {
//      "pac_string": <List of valid proxy servers, in PAC format>
//   }
//
//  Or if the the resolver failed:
//   {
//      "net_error": <Net error code that resolver failed with>
//   }
EVENT_TYPE(PROXY_SERVICE_RESOLVED_PROXY_LIST)

// ------------------------------------------------------------------------
// Proxy Resolver
// ------------------------------------------------------------------------

// Measures the time taken to execute the "myIpAddress()" javascript binding.
EVENT_TYPE(PAC_JAVASCRIPT_MY_IP_ADDRESS)

// Measures the time taken to execute the "myIpAddressEx()" javascript binding.
EVENT_TYPE(PAC_JAVASCRIPT_MY_IP_ADDRESS_EX)

// Measures the time taken to execute the "dnsResolve()" javascript binding.
EVENT_TYPE(PAC_JAVASCRIPT_DNS_RESOLVE)

// Measures the time taken to execute the "dnsResolveEx()" javascript binding.
EVENT_TYPE(PAC_JAVASCRIPT_DNS_RESOLVE_EX)

// This event is emitted when a javascript error has been triggered by a
// PAC script. It contains the following event parameters:
//   {
//      "line_number": <The line number in the PAC script
//                      (or -1 if not applicable)>,
//      "message": <The error message>
//   }
EVENT_TYPE(PAC_JAVASCRIPT_ERROR)

// This event is emitted when a PAC script called alert(). It contains the
// following event parameters:
//   {
//      "message": <The string of the alert>
//   }
EVENT_TYPE(PAC_JAVASCRIPT_ALERT)

// Measures the time that a proxy resolve request was stalled waiting for a
// proxy resolver thread to free-up.
EVENT_TYPE(WAITING_FOR_PROXY_RESOLVER_THREAD)

// This event is emitted just before a PAC request is bound to a thread. It
// contains these parameters:
//
//   {
//     "thread_number": <Identifier for the PAC thread that is going to
//                       run this request>
//   }
EVENT_TYPE(SUBMITTED_TO_RESOLVER_THREAD)

// ------------------------------------------------------------------------
// ClientSocket
// ------------------------------------------------------------------------

// The start/end of a TCP connect(). This corresponds with a call to
// TCPClientSocket::Connect().
//
// The START event contains these parameters:
//
//   {
//     "address_list": <List of network address strings>
//   }
//
// And the END event will contain the following parameters on failure:
//
//   {
//     "net_error": <Net integer error code>
//   }
EVENT_TYPE(TCP_CONNECT)

// Nested within TCP_CONNECT, there may be multiple attempts to connect
// to the individual addresses. The START event will describe the
// address the attempt is for:
//
//   {
//     "address": <String of the network address>
//   }
//
// And the END event will contain the system error code if it failed:
//
//   {
//     "os_error": <Integer error code the operating system returned>
//   }
EVENT_TYPE(TCP_CONNECT_ATTEMPT)

// Marks the begin/end of a socket (TCP/SOCKS/SSL).
EVENT_TYPE(SOCKET_ALIVE)

// This event is logged to the socket stream whenever the socket is
// acquired/released via a ClientSocketHandle.
//
// The BEGIN phase contains the following parameters:
//
//   {
//     "source_dependency": <Source identifier for the controlling entity>
//   }
EVENT_TYPE(SOCKET_IN_USE)

// The start/end of a SOCKS connect().
EVENT_TYPE(SOCKS_CONNECT)

// The start/end of a SOCKS5 connect().
EVENT_TYPE(SOCKS5_CONNECT)

// This event is emitted when the SOCKS connect fails because the provided
// was longer than 255 characters.
EVENT_TYPE(SOCKS_HOSTNAME_TOO_BIG)

// These events are emitted when insufficient data was read while
// trying to establish a connection to the SOCKS proxy server
// (during the greeting phase or handshake phase, respectively).
EVENT_TYPE(SOCKS_UNEXPECTEDLY_CLOSED_DURING_GREETING)
EVENT_TYPE(SOCKS_UNEXPECTEDLY_CLOSED_DURING_HANDSHAKE)

// This event indicates that a bad version number was received in the
// proxy server's response. The extra parameters show its value:
//   {
//     "version": <Integer version number in the response>
//   }
EVENT_TYPE(SOCKS_UNEXPECTED_VERSION)

// This event indicates that the SOCKS proxy server returned an error while
// trying to create a connection. The following parameters will be attached
// to the event:
//   {
//     "error_code": <Integer error code returned by the server>
//   }
EVENT_TYPE(SOCKS_SERVER_ERROR)

// This event indicates that the SOCKS proxy server asked for an authentication
// method that we don't support. The following parameters are attached to the
// event:
//   {
//     "method": <Integer method code>
//   }
EVENT_TYPE(SOCKS_UNEXPECTED_AUTH)

// This event indicates that the SOCKS proxy server's response indicated an
// address type which we are not prepared to handle.
// The following parameters are attached to the event:
//   {
//     "address_type": <Integer code for the address type>
//   }
EVENT_TYPE(SOCKS_UNKNOWN_ADDRESS_TYPE)

// The start/end of a SSL connect().
EVENT_TYPE(SSL_CONNECT)

// The specified number of bytes were sent on the socket.
// The following parameters are attached:
//   {
//     "num_bytes": <Number of bytes that were just sent>
//   }
EVENT_TYPE(SOCKET_BYTES_SENT)

// The specified number of bytes were received on the socket.
// The following parameters are attached:
//   {
//     "num_bytes": <Number of bytes that were just sent>
//   }
EVENT_TYPE(SOCKET_BYTES_RECEIVED)

// ------------------------------------------------------------------------
// ClientSocketPoolBase::ConnectJob
// ------------------------------------------------------------------------

// The start/end of a ConnectJob.
EVENT_TYPE(SOCKET_POOL_CONNECT_JOB)

// The start/end of the ConnectJob::Connect().
//
// The BEGIN phase has these parameters:
//
//   {
//     "group_name": <The group name for the socket request.>
//   }
EVENT_TYPE(SOCKET_POOL_CONNECT_JOB_CONNECT)

// This event is logged whenever the ConnectJob gets a new socket
// association. The event parameters point to that socket:
//
//   {
//     "source_dependency": <The source identifier for the new socket.>
//   }
EVENT_TYPE(CONNECT_JOB_SET_SOCKET)

// Whether the connect job timed out.
EVENT_TYPE(SOCKET_POOL_CONNECT_JOB_TIMED_OUT)

// ------------------------------------------------------------------------
// ClientSocketPoolBaseHelper
// ------------------------------------------------------------------------

// The start/end of a client socket pool request for a socket.
EVENT_TYPE(SOCKET_POOL)

// The request stalled because there are too many sockets in the pool.
EVENT_TYPE(SOCKET_POOL_STALLED_MAX_SOCKETS)

// The request stalled because there are too many sockets in the group.
EVENT_TYPE(SOCKET_POOL_STALLED_MAX_SOCKETS_PER_GROUP)

// Indicates that we reused an existing socket. Attached to the event are
// the parameters:
//   {
//     "idle_ms": <The number of milliseconds the socket was sitting idle for>
//   }
EVENT_TYPE(SOCKET_POOL_REUSED_AN_EXISTING_SOCKET)

// This event simply describes the host:port that were requested from the
// socket pool. Its parameters are:
//   {
//     "host_and_port": <String encoding the host and port>
//   }
EVENT_TYPE(TCP_CLIENT_SOCKET_POOL_REQUESTED_SOCKET)


// A backup socket is created due to slow connect
EVENT_TYPE(SOCKET_BACKUP_CREATED)

// This event is sent when a connect job is eventually bound to a request
// (because of late binding and socket backup jobs, we don't assign the job to
// a request until it has completed).
//
// The event parameters are:
//   {
//      "source_dependency": <Source identifer for the connect job we are
//                            bound to>
//   }
EVENT_TYPE(SOCKET_POOL_BOUND_TO_CONNECT_JOB)

// Identifies the NetLog::Source() for the Socket assigned to the pending
// request. The event parameters are:
//   {
//      "source_dependency": <Source identifier for the socket we acquired>
//   }
EVENT_TYPE(SOCKET_POOL_BOUND_TO_SOCKET)

// ------------------------------------------------------------------------
// URLRequest
// ------------------------------------------------------------------------

// Measures the time it took a URLRequestJob to start. For the most part this
// corresponds with the time between URLRequest::Start() and
// URLRequest::ResponseStarted(), however it is also repeated for every
// redirect, and every intercepted job that handles the request.
//
// For the BEGIN phase, the following parameters are attached:
//   {
//      "url": <String of URL being loaded>,
//      "method": <The method ("POST" or "GET" or "HEAD" etc..)>,
//      "load_flags": <Numeric value of the combined load flags>
//   }
//
// For the END phase, if there was an error, the following parameters are
// attached:
//   {
//      "net_error": <Net error code of the failure>
//   }
EVENT_TYPE(URL_REQUEST_START_JOB)

// This event is sent once a URLRequest receives a redirect. The parameters
// attached to the event are:
//   {
//     "location": <The URL that was redirected to>
//   }
EVENT_TYPE(URL_REQUEST_REDIRECTED)

// ------------------------------------------------------------------------
// HttpCache
// ------------------------------------------------------------------------

// Measures the time while opening a disk cache entry.
EVENT_TYPE(HTTP_CACHE_OPEN_ENTRY)

// Measures the time while creating a disk cache entry.
EVENT_TYPE(HTTP_CACHE_CREATE_ENTRY)

// Measures the time while deleting a disk cache entry.
EVENT_TYPE(HTTP_CACHE_DOOM_ENTRY)

// Measures the time while reading the response info from a disk cache entry.
EVENT_TYPE(HTTP_CACHE_READ_INFO)

// Measures the time that an HttpCache::Transaction is stalled waiting for
// the cache entry to become available (for example if we are waiting for
// exclusive access to an existing entry).
EVENT_TYPE(HTTP_CACHE_WAITING)

// ------------------------------------------------------------------------
// HttpNetworkTransaction
// ------------------------------------------------------------------------

// Measures the time taken to send the tunnel request to the server.
EVENT_TYPE(HTTP_TRANSACTION_TUNNEL_SEND_REQUEST)

// This event is sent for a tunnel request.
// The following parameters are attached:
//   {
//     "line": <The HTTP request line, CRLF terminated>,
//     "headers": <The list of header:value pairs>
//   }
EVENT_TYPE(HTTP_TRANSACTION_SEND_TUNNEL_HEADERS)

// Measures the time to read the tunnel response headers from the server.
EVENT_TYPE(HTTP_TRANSACTION_TUNNEL_READ_HEADERS)

// This event is sent on receipt of the HTTP response headers to a tunnel
// request.
// The following parameters are attached:
//   {
//     "headers": <The list of header:value pairs>
//   }
EVENT_TYPE(HTTP_TRANSACTION_READ_TUNNEL_RESPONSE_HEADERS)

// Measures the time taken to send the request to the server.
EVENT_TYPE(HTTP_TRANSACTION_SEND_REQUEST)

// This event is sent for a HTTP request.
// The following parameters are attached:
//   {
//     "line": <The HTTP request line, CRLF terminated>,
//     "headers": <The list of header:value pairs>
//   }
EVENT_TYPE(HTTP_TRANSACTION_SEND_REQUEST_HEADERS)

// Measures the time to read HTTP response headers from the server.
EVENT_TYPE(HTTP_TRANSACTION_READ_HEADERS)

// This event is sent on receipt of the HTTP response headers.
// The following parameters are attached:
//   {
//     "headers": <The list of header:value pairs>
//   }
EVENT_TYPE(HTTP_TRANSACTION_READ_RESPONSE_HEADERS)

// Measures the time to read the entity body from the server.
EVENT_TYPE(HTTP_TRANSACTION_READ_BODY)

// Measures the time taken to read the response out of the socket before
// restarting for authentication, on keep alive connections.
EVENT_TYPE(HTTP_TRANSACTION_DRAIN_BODY_FOR_AUTH_RESTART)

// ------------------------------------------------------------------------
// SpdyNetworkTransaction
// ------------------------------------------------------------------------

// Measures the time taken to get a spdy stream.
EVENT_TYPE(SPDY_TRANSACTION_INIT_CONNECTION)

// Measures the time taken to send the request to the server.
EVENT_TYPE(SPDY_TRANSACTION_SEND_REQUEST)

// Measures the time to read HTTP response headers from the server.
EVENT_TYPE(SPDY_TRANSACTION_READ_HEADERS)

// Measures the time to read the entity body from the server.
EVENT_TYPE(SPDY_TRANSACTION_READ_BODY)

// ------------------------------------------------------------------------
// SpdySession
// ------------------------------------------------------------------------

// The start/end of a SpdySession.
EVENT_TYPE(SPDY_SESSION)

// On sending a SPDY SETTINGS frame.
// The following parameters are attached:
//   {
//     "settings": <The list of setting id:value pairs>
//   }
EVENT_TYPE(SPDY_SESSION_SEND_SETTINGS)

// Receipt of a SPDY SETTINGS frame.
// The following parameters are attached:
//   {
//     "settings": <The list of setting id:value pairs>
//   }
EVENT_TYPE(SPDY_SESSION_RECV_SETTINGS)

// Receipt of a SPDY GOAWAY frame.
// The following parameters are attached:
//   {
//     "last_accepted_stream_id": <Last stream id accepted by the server, duh>
//   }
EVENT_TYPE(SPDY_SESSION_GOAWAY)

// This event is sent for a SPDY SYN_STREAM pushed by the server, but no
// URLRequest has requested it yet.
// The following parameters are attached:
//   {
//     "flags": <The control frame flags>
//     "headers": <The list of header:value pairs>
//     "id": <The stream id>
//   }
EVENT_TYPE(SPDY_SESSION_PUSHED_SYN_STREAM)

// ------------------------------------------------------------------------
// SpdyStream
// ------------------------------------------------------------------------

// This event is sent for a SPDY SYN_STREAM.
// The following parameters are attached:
//   {
//     "flags": <The control frame flags>,
//     "headers": <The list of header:value pairs>,
//     "id": <The stream id>
//   }
EVENT_TYPE(SPDY_STREAM_SYN_STREAM)

// This event is sent for a SPDY SYN_STREAM pushed by the server, where a
// URLRequest is already waiting for the stream.
// The following parameters are attached:
//   {
//     "flags": <The control frame flags>
//     "headers": <The list of header:value pairs>
//     "id": <The stream id>
//   }
EVENT_TYPE(SPDY_STREAM_PUSHED_SYN_STREAM)

// Measures the time taken to send headers on a stream.
EVENT_TYPE(SPDY_STREAM_SEND_HEADERS)

// Measures the time taken to send the body (e.g. a POST) on a stream.
EVENT_TYPE(SPDY_STREAM_SEND_BODY)

// This event is sent for a SPDY SYN_REPLY.
// The following parameters are attached:
//   {
//     "flags": <The control frame flags>,
//     "headers": <The list of header:value pairs>,
//     "id": <The stream id>
//   }
EVENT_TYPE(SPDY_STREAM_SYN_REPLY)

// Measures the time taken to read headers on a stream.
EVENT_TYPE(SPDY_STREAM_READ_HEADERS)

// Measures the time taken to read the body on a stream.
EVENT_TYPE(SPDY_STREAM_READ_BODY)

// Logs that a stream attached to a pushed stream.
EVENT_TYPE(SPDY_STREAM_ADOPTED_PUSH_STREAM)

// The receipt of a RST_STREAM
// The following parameters are attached:
//   {
//     "status": <The reason for the RST_STREAM>
//   }
EVENT_TYPE(SPDY_STREAM_RST_STREAM)

// ------------------------------------------------------------------------
// HttpStreamParser
// ------------------------------------------------------------------------

// Measures the time to read HTTP response headers from the server.
EVENT_TYPE(HTTP_STREAM_PARSER_READ_HEADERS)

// ------------------------------------------------------------------------
// SocketStream
// ------------------------------------------------------------------------

// Measures the time between SocketStream::Connect() and
// SocketStream::DidEstablishConnection()
//
// For the BEGIN phase, the following parameters are attached:
//   {
//      "url": <String of URL being loaded>
//   }
//
// For the END phase, if there was an error, the following parameters are
// attached:
//   {
//      "net_error": <Net error code of the failure>
//   }
EVENT_TYPE(SOCKET_STREAM_CONNECT)

// A message sent on the SocketStream.
EVENT_TYPE(SOCKET_STREAM_SENT)

// A message received on the SocketStream.
EVENT_TYPE(SOCKET_STREAM_RECEIVED)

// ------------------------------------------------------------------------
// SOCKS5ClientSocket
// ------------------------------------------------------------------------

// The time spent sending the "greeting" to the SOCKS server.
EVENT_TYPE(SOCKS5_GREET_WRITE)

// The time spent waiting for the "greeting" response from the SOCKS server.
EVENT_TYPE(SOCKS5_GREET_READ)

// The time spent sending the CONNECT request to the SOCKS server.
EVENT_TYPE(SOCKS5_HANDSHAKE_WRITE)

// The time spent waiting for the response to the CONNECT request.
EVENT_TYPE(SOCKS5_HANDSHAKE_READ)

// ------------------------------------------------------------------------
// HTTP Authentication
// ------------------------------------------------------------------------

// The time spent authenticating to the proxy.
EVENT_TYPE(AUTH_PROXY)

// The time spent authentication to the server.
EVENT_TYPE(AUTH_SERVER)

// ------------------------------------------------------------------------
// Global events
// ------------------------------------------------------------------------
// These are events which are not grouped by source id, as they have no
// context.

// This event is emitted whenever NetworkChangeNotifier determines that the
// underlying network has changed.
EVENT_TYPE(NETWORK_IP_ADDRESSSES_CHANGED)
