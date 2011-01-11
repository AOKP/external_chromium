// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_FLIP_PROXY_CONFIG_H
#define NET_TOOLS_FLIP_PROXY_CONFIG_H
#pragma once

#include <arpa/inet.h>  // in_addr_t

#include "base/logging.h"
#include "net/tools/flip_server/create_listener.h"

#include <vector>
#include <string>

using std::string;
using std::vector;

enum FlipHandlerType {
    FLIP_HANDLER_PROXY,
    FLIP_HANDLER_SPDY_SERVER,
    FLIP_HANDLER_HTTP_SERVER
};

class FlipAcceptor {
public:
   enum FlipHandlerType flip_handler_type_;
   string listen_ip_;
   string listen_port_;
   string ssl_cert_filename_;
   string ssl_key_filename_;
   string server_ip_;
   string server_port_;
   int accept_backlog_size_;
   bool disable_nagle_;
   int accepts_per_wake_;
   int listen_fd_;
   void* memory_cache_;

   FlipAcceptor(enum FlipHandlerType flip_handler_type,
                string listen_ip,
                string listen_port,
                string ssl_cert_filename,
                string ssl_key_filename,
                string server_ip,
                string server_port,
                int accept_backlog_size,
                bool disable_nagle,
                int accepts_per_wake,
                bool reuseport,
                bool wait_for_iface,
                void *memory_cache) :
       flip_handler_type_(flip_handler_type),
       listen_ip_(listen_ip),
       listen_port_(listen_port),
       ssl_cert_filename_(ssl_cert_filename),
       ssl_key_filename_(ssl_key_filename),
       server_ip_(server_ip),
       server_port_(server_port),
       accept_backlog_size_(accept_backlog_size),
       disable_nagle_(disable_nagle),
       accepts_per_wake_(accepts_per_wake),
       memory_cache_(memory_cache)
   {
     VLOG(1) << "Attempting to listen on " << listen_ip_.c_str() << ":"
             << listen_port_.c_str();
     while (1) {
       int ret = net::CreateListeningSocket(listen_ip_,
                                            listen_port_,
                                            true,
                                            accept_backlog_size_,
                                            true,
                                            reuseport,
                                            wait_for_iface,
                                            disable_nagle_,
                                            &listen_fd_);
       if ( ret == 0 ) {
         break;
       } else if ( ret == -3 && wait_for_iface ) {
         // Binding error EADDRNOTAVAIL was encounted. We need
         // to wait for the interfaces to raised. try again.
         usleep(200000);
       } else {
         LOG(ERROR) << "Unable to create listening socket for: ret = " << ret
                    << ": " << listen_ip_.c_str() << ":"
                    << listen_port_.c_str();
         return;
       }
     }
     net::SetNonBlocking(listen_fd_);
     VLOG(1) << "Listening for spdy on port: " << listen_ip_ << ":"
             << listen_port_;
   }
   ~FlipAcceptor () {}
};

class FlipConfig {
public:
   std::vector <FlipAcceptor*> acceptors_;
   double server_think_time_in_s_;
   enum logging::LoggingDestination log_destination_;
   string log_filename_;
   bool forward_ip_header_enabled_;
   string forward_ip_header_;
   bool wait_for_iface_;
   int ssl_session_expiry_;

   FlipConfig() :
       server_think_time_in_s_(0),
       log_destination_(logging::LOG_ONLY_TO_SYSTEM_DEBUG_LOG),
       forward_ip_header_enabled_(false),
       wait_for_iface_(false),
       ssl_session_expiry_(300)
       {}

   ~FlipConfig() {}

   void AddAcceptor(enum FlipHandlerType flip_handler_type,
                    string listen_ip,
                    string listen_port,
                    string ssl_cert_filename,
                    string ssl_key_filename,
                    string server_ip,
                    string server_port,
                    int accept_backlog_size,
                    bool disable_nagle,
                    int accepts_per_wake,
                    bool reuseport,
                    bool wait_for_iface,
                    void *memory_cache) {
       // TODO(mbelshe): create a struct FlipConfigArgs{} for the arguments.
       acceptors_.push_back(new FlipAcceptor(flip_handler_type,
                                             listen_ip,
                                             listen_port,
                                             ssl_cert_filename,
                                             ssl_key_filename,
                                             server_ip,
                                             server_port,
                                             accept_backlog_size,
                                             disable_nagle,
                                             accepts_per_wake,
                                             reuseport,
                                             wait_for_iface,
                                             memory_cache));
   }

};

#endif
