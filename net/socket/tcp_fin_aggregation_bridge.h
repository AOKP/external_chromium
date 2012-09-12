// Copyright (c) 2011, Code Aurora Forum. All rights reserved.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Code Aurora Forum, Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.

// THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
// WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
// ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
// BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
// BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
// OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
// IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef TCP_FIN_AGGREGATION_BRIDGE_H_
#define TCP_FIN_AGGREGATION_BRIDGE_H_

#include "client_socket_pool_base.h"
#include "time.h"

namespace net {
namespace internal {
  class ClientSocketPoolBaseHelper;
  class IdleSocket;
}
};

extern void DecrementIdleCount(net::internal::ClientSocketPoolBaseHelper* pool_base_helper) __attribute__((visibility("default"), used));
extern void RemoveGroup(net::internal::ClientSocketPoolBaseHelper* pool_base_helper, const std::string& group_name) __attribute__((visibility("default"), used));
extern bool ShouldCleanup(net::internal::IdleSocket* idle_socket, base::Time now, base::TimeDelta timeout) __attribute__((visibility("default"), used));
extern base::Time GetCurrentTime() __attribute__((visibility("default"), used));

#endif /* TCP_FIN_AGGREGATION_BRIDGE_H_ */
