// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_CLIENT_SOCKET_FACTORY_H_
#define NET_SOCKET_CLIENT_SOCKET_FACTORY_H_
#pragma once

#include <string>

#include "base/basictypes.h"
#include "net/base/net_log.h"

namespace net {

class AddressList;
class CertVerifier;
class ClientSocket;
class ClientSocketHandle;
class DnsCertProvenanceChecker;
class HostPortPair;
class SSLClientSocket;
struct SSLConfig;
class SSLHostInfo;

// Callback function to create new SSLClientSocket objects.
typedef SSLClientSocket* (*SSLClientSocketFactory)(
    ClientSocketHandle* transport_socket,
    const HostPortPair& host_and_port,
    const SSLConfig& ssl_config,
    SSLHostInfo* ssl_host_info,
    CertVerifier* cert_verifier,
    DnsCertProvenanceChecker* dns_cert_checker);

// An interface used to instantiate ClientSocket objects.  Used to facilitate
// testing code with mock socket implementations.
class ClientSocketFactory {
 public:
  virtual ~ClientSocketFactory() {}

  // |source| is the NetLog::Source for the entity trying to create the socket,
  // if it has one.
  virtual ClientSocket* CreateTCPClientSocket(
      const AddressList& addresses,
      NetLog* net_log,
      const NetLog::Source& source) = 0;

  virtual SSLClientSocket* CreateSSLClientSocket(
      ClientSocketHandle* transport_socket,
      const HostPortPair& host_and_port,
      const SSLConfig& ssl_config,
      SSLHostInfo* ssl_host_info,
      CertVerifier* cert_verifier,
      DnsCertProvenanceChecker* dns_cert_checker) = 0;

  // Deprecated function (http://crbug.com/37810) that takes a ClientSocket.
  virtual SSLClientSocket* CreateSSLClientSocket(
      ClientSocket* transport_socket,
      const HostPortPair& host_and_port,
      const SSLConfig& ssl_config,
      SSLHostInfo* ssl_host_info,
      CertVerifier* cert_verifier);

  // Returns the default ClientSocketFactory.
  static ClientSocketFactory* GetDefaultFactory();

  // Instructs the default ClientSocketFactory to use |factory| to create
  // SSLClientSocket objects.
  static void SetSSLClientSocketFactory(SSLClientSocketFactory factory);
};

}  // namespace net

#endif  // NET_SOCKET_CLIENT_SOCKET_FACTORY_H_
