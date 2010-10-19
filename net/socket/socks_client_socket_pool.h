// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_SOCKS_CLIENT_SOCKET_POOL_H_
#define NET_SOCKET_SOCKS_CLIENT_SOCKET_POOL_H_
#pragma once

#include <string>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/ref_counted.h"
#include "base/scoped_ptr.h"
#include "base/time.h"
#include "net/base/host_port_pair.h"
#include "net/base/host_resolver.h"
#include "net/socket/client_socket_pool_base.h"
#include "net/socket/client_socket_pool_histograms.h"
#include "net/socket/client_socket_pool.h"

namespace net {

class ConnectJobFactory;
class TCPClientSocketPool;
class TCPSocketParams;

class SOCKSSocketParams : public base::RefCounted<SOCKSSocketParams> {
 public:
  SOCKSSocketParams(const scoped_refptr<TCPSocketParams>& proxy_server,
                    bool socks_v5, const HostPortPair& host_port_pair,
                    RequestPriority priority, const GURL& referrer);

  const scoped_refptr<TCPSocketParams>& tcp_params() const {
    return tcp_params_;
  }
  const HostResolver::RequestInfo& destination() const { return destination_; }
  bool is_socks_v5() const { return socks_v5_; }

 private:
  friend class base::RefCounted<SOCKSSocketParams>;
  ~SOCKSSocketParams();

  // The tcp connection must point toward the proxy server.
  const scoped_refptr<TCPSocketParams> tcp_params_;
  // This is the HTTP destination.
  HostResolver::RequestInfo destination_;
  const bool socks_v5_;

  DISALLOW_COPY_AND_ASSIGN(SOCKSSocketParams);
};

// SOCKSConnectJob handles the handshake to a socks server after setting up
// an underlying transport socket.
class SOCKSConnectJob : public ConnectJob {
 public:
  SOCKSConnectJob(const std::string& group_name,
                  const scoped_refptr<SOCKSSocketParams>& params,
                  const base::TimeDelta& timeout_duration,
                  TCPClientSocketPool* tcp_pool,
                  const scoped_refptr<HostResolver> &host_resolver,
                  Delegate* delegate,
                  NetLog* net_log);
  virtual ~SOCKSConnectJob();

  // ConnectJob methods.
  virtual LoadState GetLoadState() const;

 private:
  enum State {
    STATE_TCP_CONNECT,
    STATE_TCP_CONNECT_COMPLETE,
    STATE_SOCKS_CONNECT,
    STATE_SOCKS_CONNECT_COMPLETE,
    STATE_NONE,
  };

  // Begins the tcp connection and the SOCKS handshake.  Returns OK on success
  // and ERR_IO_PENDING if it cannot immediately service the request.
  // Otherwise, it returns a net error code.
  virtual int ConnectInternal();

  void OnIOComplete(int result);

  // Runs the state transition loop.
  int DoLoop(int result);

  int DoTCPConnect();
  int DoTCPConnectComplete(int result);
  int DoSOCKSConnect();
  int DoSOCKSConnectComplete(int result);

  scoped_refptr<SOCKSSocketParams> socks_params_;
  TCPClientSocketPool* const tcp_pool_;
  const scoped_refptr<HostResolver> resolver_;

  State next_state_;
  CompletionCallbackImpl<SOCKSConnectJob> callback_;
  scoped_ptr<ClientSocketHandle> tcp_socket_handle_;
  scoped_ptr<ClientSocket> socket_;

  DISALLOW_COPY_AND_ASSIGN(SOCKSConnectJob);
};

class SOCKSClientSocketPool : public ClientSocketPool {
 public:
  SOCKSClientSocketPool(
      int max_sockets,
      int max_sockets_per_group,
      ClientSocketPoolHistograms* histograms,
      const scoped_refptr<HostResolver>& host_resolver,
      TCPClientSocketPool* tcp_pool,
      NetLog* net_log);

  virtual ~SOCKSClientSocketPool();

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

  virtual DictionaryValue* GetInfoAsValue(const std::string& name,
                                          const std::string& type,
                                          bool include_nested_pools) const;

  virtual base::TimeDelta ConnectionTimeout() const {
    return base_.ConnectionTimeout();
  }

  virtual ClientSocketPoolHistograms* histograms() const {
    return base_.histograms();
  };

 private:
  typedef ClientSocketPoolBase<SOCKSSocketParams> PoolBase;

  class SOCKSConnectJobFactory : public PoolBase::ConnectJobFactory {
   public:
    SOCKSConnectJobFactory(TCPClientSocketPool* tcp_pool,
                           HostResolver* host_resolver,
                           NetLog* net_log)
        : tcp_pool_(tcp_pool),
          host_resolver_(host_resolver),
          net_log_(net_log) {}

    virtual ~SOCKSConnectJobFactory() {}

    // ClientSocketPoolBase::ConnectJobFactory methods.
    virtual ConnectJob* NewConnectJob(
        const std::string& group_name,
        const PoolBase::Request& request,
        ConnectJob::Delegate* delegate) const;

    virtual base::TimeDelta ConnectionTimeout() const;

   private:
    TCPClientSocketPool* const tcp_pool_;
    const scoped_refptr<HostResolver> host_resolver_;
    NetLog* net_log_;

    DISALLOW_COPY_AND_ASSIGN(SOCKSConnectJobFactory);
  };

  TCPClientSocketPool* const tcp_pool_;
  PoolBase base_;

  DISALLOW_COPY_AND_ASSIGN(SOCKSClientSocketPool);
};

REGISTER_SOCKET_PARAMS_FOR_POOL(SOCKSClientSocketPool, SOCKSSocketParams);

}  // namespace net

#endif  // NET_SOCKET_SOCKS_CLIENT_SOCKET_POOL_H_
