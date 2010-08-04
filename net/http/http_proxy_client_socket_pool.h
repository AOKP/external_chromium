// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_HTTP_PROXY_CLIENT_SOCKET_POOL_H_
#define NET_HTTP_HTTP_PROXY_CLIENT_SOCKET_POOL_H_

#include <string>

#include "base/basictypes.h"
#include "base/ref_counted.h"
#include "base/scoped_ptr.h"
#include "base/time.h"
#include "net/base/host_port_pair.h"
#include "net/base/host_resolver.h"
#include "net/http/http_auth.h"
#include "net/proxy/proxy_server.h"
#include "net/socket/client_socket_pool_base.h"
#include "net/socket/client_socket_pool_histograms.h"
#include "net/socket/client_socket_pool.h"
#include "net/socket/tcp_client_socket_pool.h"

namespace net {

class ClientSocketFactory;
class ConnectJobFactory;
class HttpAuthController;

class HttpProxySocketParams : public base::RefCounted<HttpProxySocketParams> {
 public:
  HttpProxySocketParams(const scoped_refptr<TCPSocketParams>& proxy_server,
                        const GURL& request_url, HostPortPair endpoint,
                        scoped_refptr<HttpAuthController> auth_controller,
                        bool tunnel);

  const scoped_refptr<TCPSocketParams>& tcp_params() const {
    return tcp_params_;
  }
  const GURL& request_url() const { return request_url_; }
  const HostPortPair& endpoint() const { return endpoint_; }
  const scoped_refptr<HttpAuthController>& auth_controller() {
    return auth_controller_;
  }
  bool tunnel() const { return tunnel_; }

 private:
  friend class base::RefCounted<HttpProxySocketParams>;
  ~HttpProxySocketParams();

  const scoped_refptr<TCPSocketParams> tcp_params_;
  const GURL request_url_;
  const HostPortPair endpoint_;
  const scoped_refptr<HttpAuthController> auth_controller_;
  const bool tunnel_;

  DISALLOW_COPY_AND_ASSIGN(HttpProxySocketParams);
};

// HttpProxyConnectJob optionally establishes a tunnel through the proxy
// server after connecting the underlying transport socket.
class HttpProxyConnectJob : public ConnectJob {
 public:
  HttpProxyConnectJob(const std::string& group_name,
                      const scoped_refptr<HttpProxySocketParams>& params,
                      const base::TimeDelta& timeout_duration,
                      const scoped_refptr<TCPClientSocketPool>& tcp_pool,
                      const scoped_refptr<HostResolver> &host_resolver,
                      Delegate* delegate,
                      NetLog* net_log);
  virtual ~HttpProxyConnectJob();

  // ConnectJob methods.
  virtual LoadState GetLoadState() const;

 private:
  enum State {
    kStateTCPConnect,
    kStateTCPConnectComplete,
    kStateHttpProxyConnect,
    kStateHttpProxyConnectComplete,
    kStateNone,
  };

  // Begins the tcp connection and the optional Http proxy tunnel.  If the
  // request is not immediately servicable (likely), the request will return
  // ERR_IO_PENDING. An OK return from this function or the callback means
  // that the connection is established; ERR_PROXY_AUTH_REQUESTED means
  // that the tunnel needs authentication credentials, the socket will be
  // returned in this case, and must be release back to the pool; or
  // a standard net error code will be returned.
  virtual int ConnectInternal();

  void OnIOComplete(int result);

  // Runs the state transition loop.
  int DoLoop(int result);

  int DoTCPConnect();
  int DoTCPConnectComplete(int result);
  int DoHttpProxyConnect();
  int DoHttpProxyConnectComplete(int result);

  scoped_refptr<HttpProxySocketParams> params_;
  const scoped_refptr<TCPClientSocketPool> tcp_pool_;
  const scoped_refptr<HostResolver> resolver_;

  State next_state_;
  CompletionCallbackImpl<HttpProxyConnectJob> callback_;
  scoped_ptr<ClientSocketHandle> tcp_socket_handle_;
  scoped_ptr<ClientSocket> socket_;

  DISALLOW_COPY_AND_ASSIGN(HttpProxyConnectJob);
};

class HttpProxyClientSocketPool : public ClientSocketPool {
 public:
  HttpProxyClientSocketPool(
      int max_sockets,
      int max_sockets_per_group,
      const scoped_refptr<ClientSocketPoolHistograms>& histograms,
      const scoped_refptr<HostResolver>& host_resolver,
      const scoped_refptr<TCPClientSocketPool>& tcp_pool,
      NetLog* net_log);

  // ClientSocketPool methods:
  virtual int RequestSocket(const std::string& group_name,
                            const void* connect_params,
                            RequestPriority priority,
                            ClientSocketHandle* handle,
                            CompletionCallback* callback,
                            const BoundNetLog& net_log);

  virtual void CancelRequest(const std::string& group_name,
                             ClientSocketHandle* handle);

  virtual void ReleaseSocket(const std::string& group_name,
                             ClientSocket* socket,
                             int id);

  virtual void Flush();

  virtual void CloseIdleSockets();

  virtual int IdleSocketCount() const {
    return base_.idle_socket_count();
  }

  virtual int IdleSocketCountInGroup(const std::string& group_name) const;

  virtual LoadState GetLoadState(const std::string& group_name,
                                 const ClientSocketHandle* handle) const;

  virtual base::TimeDelta ConnectionTimeout() const {
    return base_.ConnectionTimeout();
  }

  virtual scoped_refptr<ClientSocketPoolHistograms> histograms() const {
    return base_.histograms();
  };

 protected:
  virtual ~HttpProxyClientSocketPool();

 private:
  typedef ClientSocketPoolBase<HttpProxySocketParams> PoolBase;

  class HttpProxyConnectJobFactory : public PoolBase::ConnectJobFactory {
   public:
    HttpProxyConnectJobFactory(
        const scoped_refptr<TCPClientSocketPool>& tcp_pool,
        HostResolver* host_resolver,
        NetLog* net_log)
        : tcp_pool_(tcp_pool),
          host_resolver_(host_resolver),
          net_log_(net_log) {}

    virtual ~HttpProxyConnectJobFactory() {}

    // ClientSocketPoolBase::ConnectJobFactory methods.
    virtual ConnectJob* NewConnectJob(const std::string& group_name,
                                      const PoolBase::Request& request,
                                      ConnectJob::Delegate* delegate) const;

    virtual base::TimeDelta ConnectionTimeout() const;

   private:
    const scoped_refptr<TCPClientSocketPool> tcp_pool_;
    const scoped_refptr<HostResolver> host_resolver_;
    NetLog* net_log_;

    DISALLOW_COPY_AND_ASSIGN(HttpProxyConnectJobFactory);
  };

  PoolBase base_;

  DISALLOW_COPY_AND_ASSIGN(HttpProxyClientSocketPool);
};

REGISTER_SOCKET_PARAMS_FOR_POOL(HttpProxyClientSocketPool,
                                HttpProxySocketParams);

}  // namespace net

#endif  // NET_HTTP_HTTP_PROXY_CLIENT_SOCKET_POOL_H_
