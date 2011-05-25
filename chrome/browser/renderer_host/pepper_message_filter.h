// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_RENDERER_HOST_PEPPER_MESSAGE_FILTER_H_
#define CHROME_BROWSER_RENDERER_HOST_PEPPER_MESSAGE_FILTER_H_
#pragma once

#include <string>

#include "base/basictypes.h"
#include "base/process.h"
#include "chrome/browser/browser_message_filter.h"
#include "ipc/ipc_channel_proxy.h"
#include "ppapi/c/private/ppb_flash.h"

class Profile;

namespace net {
class AddressList;
}

class PepperMessageFilter : public BrowserMessageFilter {
 public:
  explicit PepperMessageFilter(Profile* profile);

 private:
  // BrowserMessageFilter methods.
  virtual bool OnMessageReceived(const IPC::Message& message,
                                 bool* message_was_ok);

#if defined(ENABLE_FLAPPER_HACKS)
  // Message handlers.
  void OnPepperConnectTcp(int routing_id,
                          int request_id,
                          const std::string& host,
                          uint16 port);
  void OnPepperConnectTcpAddress(int routing_id,
                                 int request_id,
                                 const PP_Flash_NetAddress& address);

  // |Send()| a |ViewMsg_PepperConnectTcpACK|, which reports an error.
  bool SendPepperConnectTcpACKError(int routing_id,
                                    int request_id);

  // Used by |OnPepperConnectTcp()| (below).
  class LookupRequest;
  friend class LookupRequest;

  // Continuation of |OnPepperConnectTcp()|.
  void PepperConnectTcpLookupFinished(int routing_id,
                                      int request_id,
                                      const net::AddressList& addresses);
  void PepperConnectTcpOnWorkerThread(int routing_id,
                                      int request_id,
                                      net::AddressList addresses);

  // Continuation of |OnPepperConnectTcpAddress()|.
  void PepperConnectTcpAddressOnWorkerThread(int routing_id,
                                             int request_id,
                                             PP_Flash_NetAddress addr);
#endif  // ENABLE_FLAPPER_HACKS

  Profile* profile_;
};

#endif  // CHROME_BROWSER_RENDERER_HOST_PEPPER_MESSAGE_FILTER_H_
