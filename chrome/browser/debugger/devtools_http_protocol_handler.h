// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_DEBUGGER_DEVTOOLS_HTTP_PROTOCOL_HANDLER_H_
#define CHROME_BROWSER_DEBUGGER_DEVTOOLS_HTTP_PROTOCOL_HANDLER_H_
#pragma once

#include <set>
#include <string>

#include "base/ref_counted.h"
#include "net/server/http_listen_socket.h"
#include "net/url_request/url_request.h"

class DevToolsClientHost;
class DevToolsHttpServer;
class TabContents;

class DevToolsHttpProtocolHandler
    : public HttpListenSocket::Delegate,
      public URLRequest::Delegate,
      public base::RefCountedThreadSafe<DevToolsHttpProtocolHandler> {
 public:
  explicit DevToolsHttpProtocolHandler(int port);

  // This method should be called after the object construction.
  void Start();

  // This method should be called before the object destruction.
  void Stop();

 private:
  friend class base::RefCountedThreadSafe<DevToolsHttpProtocolHandler>;
  virtual ~DevToolsHttpProtocolHandler();

  // HttpListenSocket::Delegate implementation.
  virtual void OnHttpRequest(HttpListenSocket* socket,
                             const HttpServerRequestInfo& info);
  virtual void OnWebSocketRequest(HttpListenSocket* socket,
                                  const HttpServerRequestInfo& info);
  virtual void OnWebSocketMessage(HttpListenSocket* socket,
                                  const std::string& data);
  virtual void OnClose(HttpListenSocket* socket);

  virtual void OnHttpRequestUI(HttpListenSocket* socket,
                               const HttpServerRequestInfo& info);
  virtual void OnWebSocketRequestUI(HttpListenSocket* socket,
                                    const HttpServerRequestInfo& info);
  virtual void OnWebSocketMessageUI(HttpListenSocket* socket,
                                    const std::string& data);
  virtual void OnCloseUI(HttpListenSocket* socket);

  // URLRequest::Delegate implementation.
  virtual void OnResponseStarted(URLRequest* request);
  virtual void OnReadCompleted(URLRequest* request, int bytes_read);

  void Init();
  void Teardown();
  void Bind(URLRequest* request, HttpListenSocket* socket);
  void RequestCompleted(URLRequest* request);

  void Send200(HttpListenSocket* socket,
               const std::string& data,
               const std::string& mime_type = "text/html");
  void Send404(HttpListenSocket* socket);
  void Send500(HttpListenSocket* socket,
               const std::string& message);
  void AcceptWebSocket(HttpListenSocket* socket,
                       const HttpServerRequestInfo& request);

  TabContents* GetTabContents(int session_id);

  int port_;
  scoped_refptr<HttpListenSocket> server_;
  typedef std::map<URLRequest*, HttpListenSocket*>
      RequestToSocketMap;
  RequestToSocketMap request_to_socket_io_;
  typedef std::map<HttpListenSocket*, std::set<URLRequest*> >
      SocketToRequestsMap;
  SocketToRequestsMap socket_to_requests_io_;
  typedef std::map<URLRequest*, scoped_refptr<net::IOBuffer> >
      BuffersMap;
  BuffersMap request_to_buffer_io_;
  typedef std::map<HttpListenSocket*, DevToolsClientHost*>
      SocketToClientHostMap;
  SocketToClientHostMap socket_to_client_host_ui_;
  DISALLOW_COPY_AND_ASSIGN(DevToolsHttpProtocolHandler);
};

#endif  // CHROME_BROWSER_DEBUGGER_DEVTOOLS_HTTP_PROTOCOL_HANDLER_H_
