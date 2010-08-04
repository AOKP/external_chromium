// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/socks_client_socket_pool.h"

#include "base/time.h"
#include "googleurl/src/gurl.h"
#include "net/base/net_errors.h"
#include "net/socket/client_socket_factory.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/client_socket_pool_base.h"
#include "net/socket/socks5_client_socket.h"
#include "net/socket/socks_client_socket.h"

namespace net {

SOCKSSocketParams::SOCKSSocketParams(
    const scoped_refptr<TCPSocketParams>& proxy_server,
    bool socks_v5,
    const HostPortPair& host_port_pair,
    RequestPriority priority,
    const GURL& referrer)
    : tcp_params_(proxy_server),
      destination_(host_port_pair.host, host_port_pair.port),
      socks_v5_(socks_v5) {
  // The referrer is used by the DNS prefetch system to correlate resolutions
  // with the page that triggered them. It doesn't impact the actual addresses
  // that we resolve to.
  destination_.set_referrer(referrer);
  destination_.set_priority(priority);
}

SOCKSSocketParams::~SOCKSSocketParams() {}

// SOCKSConnectJobs will time out after this many seconds.  Note this is on
// top of the timeout for the transport socket.
static const int kSOCKSConnectJobTimeoutInSeconds = 30;

SOCKSConnectJob::SOCKSConnectJob(
    const std::string& group_name,
    const scoped_refptr<SOCKSSocketParams>& socks_params,
    const base::TimeDelta& timeout_duration,
    const scoped_refptr<TCPClientSocketPool>& tcp_pool,
    const scoped_refptr<HostResolver>& host_resolver,
    Delegate* delegate,
    NetLog* net_log)
    : ConnectJob(group_name, timeout_duration, delegate,
                 BoundNetLog::Make(net_log, NetLog::SOURCE_CONNECT_JOB)),
      socks_params_(socks_params),
      tcp_pool_(tcp_pool),
      resolver_(host_resolver),
      ALLOW_THIS_IN_INITIALIZER_LIST(
          callback_(this, &SOCKSConnectJob::OnIOComplete)) {
}

SOCKSConnectJob::~SOCKSConnectJob() {
  // We don't worry about cancelling the tcp socket since the destructor in
  // scoped_ptr<ClientSocketHandle> tcp_socket_handle_ will take care of it.
}

LoadState SOCKSConnectJob::GetLoadState() const {
  switch (next_state_) {
    case kStateTCPConnect:
    case kStateTCPConnectComplete:
      return tcp_socket_handle_->GetLoadState();
    case kStateSOCKSConnect:
    case kStateSOCKSConnectComplete:
      return LOAD_STATE_CONNECTING;
    default:
      NOTREACHED();
      return LOAD_STATE_IDLE;
  }
}

int SOCKSConnectJob::ConnectInternal() {
  next_state_ = kStateTCPConnect;
  return DoLoop(OK);
}

void SOCKSConnectJob::OnIOComplete(int result) {
  int rv = DoLoop(result);
  if (rv != ERR_IO_PENDING)
    NotifyDelegateOfCompletion(rv);  // Deletes |this|
}

int SOCKSConnectJob::DoLoop(int result) {
  DCHECK_NE(next_state_, kStateNone);

  int rv = result;
  do {
    State state = next_state_;
    next_state_ = kStateNone;
    switch (state) {
      case kStateTCPConnect:
        DCHECK_EQ(OK, rv);
        rv = DoTCPConnect();
        break;
      case kStateTCPConnectComplete:
        rv = DoTCPConnectComplete(rv);
        break;
      case kStateSOCKSConnect:
        DCHECK_EQ(OK, rv);
        rv = DoSOCKSConnect();
        break;
      case kStateSOCKSConnectComplete:
        rv = DoSOCKSConnectComplete(rv);
        break;
      default:
        NOTREACHED() << "bad state";
        rv = ERR_FAILED;
        break;
    }
  } while (rv != ERR_IO_PENDING && next_state_ != kStateNone);

  return rv;
}

int SOCKSConnectJob::DoTCPConnect() {
  next_state_ = kStateTCPConnectComplete;
  tcp_socket_handle_.reset(new ClientSocketHandle());
  return tcp_socket_handle_->Init(group_name(), socks_params_->tcp_params(),
                                  socks_params_->destination().priority(),
                                  &callback_, tcp_pool_, net_log());
}

int SOCKSConnectJob::DoTCPConnectComplete(int result) {
  if (result != OK)
    return result;

  // Reset the timer to just the length of time allowed for SOCKS handshake
  // so that a fast TCP connection plus a slow SOCKS failure doesn't take
  // longer to timeout than it should.
  ResetTimer(base::TimeDelta::FromSeconds(kSOCKSConnectJobTimeoutInSeconds));
  next_state_ = kStateSOCKSConnect;
  return result;
}

int SOCKSConnectJob::DoSOCKSConnect() {
  next_state_ = kStateSOCKSConnectComplete;

  // Add a SOCKS connection on top of the tcp socket.
  if (socks_params_->is_socks_v5()) {
    socket_.reset(new SOCKS5ClientSocket(tcp_socket_handle_.release(),
                                         socks_params_->destination()));
  } else {
    socket_.reset(new SOCKSClientSocket(tcp_socket_handle_.release(),
                                        socks_params_->destination(),
                                        resolver_));
  }
  return socket_->Connect(&callback_);
}

int SOCKSConnectJob::DoSOCKSConnectComplete(int result) {
  if (result != OK) {
    socket_->Disconnect();
    return result;
  }

  set_socket(socket_.release());
  return result;
}

ConnectJob* SOCKSClientSocketPool::SOCKSConnectJobFactory::NewConnectJob(
    const std::string& group_name,
    const PoolBase::Request& request,
    ConnectJob::Delegate* delegate) const {
  return new SOCKSConnectJob(group_name, request.params(), ConnectionTimeout(),
                             tcp_pool_, host_resolver_, delegate, net_log_);
}

base::TimeDelta
SOCKSClientSocketPool::SOCKSConnectJobFactory::ConnectionTimeout() const {
  return tcp_pool_->ConnectionTimeout() +
      base::TimeDelta::FromSeconds(kSOCKSConnectJobTimeoutInSeconds);
}

SOCKSClientSocketPool::SOCKSClientSocketPool(
    int max_sockets,
    int max_sockets_per_group,
    const scoped_refptr<ClientSocketPoolHistograms>& histograms,
    const scoped_refptr<HostResolver>& host_resolver,
    const scoped_refptr<TCPClientSocketPool>& tcp_pool,
    NetLog* net_log)
    : base_(max_sockets, max_sockets_per_group, histograms,
            base::TimeDelta::FromSeconds(
                ClientSocketPool::unused_idle_socket_timeout()),
            base::TimeDelta::FromSeconds(kUsedIdleSocketTimeout),
            new SOCKSConnectJobFactory(tcp_pool, host_resolver, net_log)) {
}

SOCKSClientSocketPool::~SOCKSClientSocketPool() {}

int SOCKSClientSocketPool::RequestSocket(const std::string& group_name,
                                         const void* socket_params,
                                         RequestPriority priority,
                                         ClientSocketHandle* handle,
                                         CompletionCallback* callback,
                                         const BoundNetLog& net_log) {
  const scoped_refptr<SOCKSSocketParams>* casted_socket_params =
      static_cast<const scoped_refptr<SOCKSSocketParams>*>(socket_params);

  return base_.RequestSocket(group_name, *casted_socket_params, priority,
                             handle, callback, net_log);
}

void SOCKSClientSocketPool::CancelRequest(const std::string& group_name,
                                          ClientSocketHandle* handle) {
  base_.CancelRequest(group_name, handle);
}

void SOCKSClientSocketPool::ReleaseSocket(const std::string& group_name,
                                          ClientSocket* socket, int id) {
  base_.ReleaseSocket(group_name, socket, id);
}

void SOCKSClientSocketPool::Flush() {
  base_.Flush();
}

void SOCKSClientSocketPool::CloseIdleSockets() {
  base_.CloseIdleSockets();
}

int SOCKSClientSocketPool::IdleSocketCountInGroup(
    const std::string& group_name) const {
  return base_.IdleSocketCountInGroup(group_name);
}

LoadState SOCKSClientSocketPool::GetLoadState(
    const std::string& group_name, const ClientSocketHandle* handle) const {
  return base_.GetLoadState(group_name, handle);
}

}  // namespace net
