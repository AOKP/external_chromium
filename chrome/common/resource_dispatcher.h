// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// See http://dev.chromium.org/developers/design-documents/multi-process-resource-loading

#ifndef CHROME_COMMON_RESOURCE_DISPATCHER_H__
#define CHROME_COMMON_RESOURCE_DISPATCHER_H__

#include <deque>
#include <string>

#include "base/hash_tables.h"
#include "base/shared_memory.h"
#include "base/task.h"
#include "ipc/ipc_channel.h"
#include "webkit/glue/resource_loader_bridge.h"

struct ResourceResponseHead;

// This class serves as a communication interface between the
// ResourceDispatcherHost in the browser process and the ResourceLoaderBridge in
// the child process.  It can be used from any child process.
class ResourceDispatcher {
 public:
  explicit ResourceDispatcher(IPC::Message::Sender* sender);
  ~ResourceDispatcher();

  // Called to possibly handle the incoming IPC message.  Returns true if
  // handled, else false.
  bool OnMessageReceived(const IPC::Message& message);

  // Creates a ResourceLoaderBridge for this type of dispatcher, this is so
  // this can be tested regardless of the ResourceLoaderBridge::Create
  // implementation.
  webkit_glue::ResourceLoaderBridge* CreateBridge(
      const webkit_glue::ResourceLoaderBridge::RequestInfo& request_info,
      int host_renderer_id,
      int host_render_view_id);

  // Adds a request from the pending_requests_ list, returning the new
  // requests' ID
  int AddPendingRequest(webkit_glue::ResourceLoaderBridge::Peer* callback,
                        ResourceType::Type resource_type,
                        const GURL& request_url);

  // Removes a request from the pending_requests_ list, returning true if the
  // request was found and removed.
  bool RemovePendingRequest(int request_id);

  // Cancels a request in the pending_requests_ list.
  void CancelPendingRequest(int routing_id, int request_id);

  IPC::Message::Sender* message_sender() const {
    return message_sender_;
  }

  // Toggles the is_deferred attribute for the specified request.
  void SetDefersLoading(int request_id, bool value);

 private:
  friend class ResourceDispatcherTest;

  typedef std::deque<IPC::Message*> MessageQueue;
  struct PendingRequestInfo {
    PendingRequestInfo() { }
    PendingRequestInfo(webkit_glue::ResourceLoaderBridge::Peer* peer,
                       ResourceType::Type resource_type,
                       const GURL& request_url)
        : peer(peer),
          resource_type(resource_type),
          is_deferred(false),
          url(request_url) {
    }
    ~PendingRequestInfo() { }
    webkit_glue::ResourceLoaderBridge::Peer* peer;
    ResourceType::Type resource_type;
    MessageQueue deferred_message_queue;
    bool is_deferred;
    GURL url;
  };
  typedef base::hash_map<int, PendingRequestInfo> PendingRequestList;

  // Message response handlers, called by the message handler for this process.
  void OnUploadProgress(
      const IPC::Message& message,
      int request_id,
      int64 position,
      int64 size);
  void OnReceivedResponse(int request_id, const ResourceResponseHead&);
  void OnReceivedCachedMetadata(int request_id, const std::vector<char>& data);
  void OnReceivedRedirect(
      const IPC::Message& message,
      int request_id,
      const GURL& new_url,
      const webkit_glue::ResourceLoaderBridge::ResponseInfo& info);
  void OnReceivedData(
      const IPC::Message& message,
      int request_id,
      base::SharedMemoryHandle data,
      int data_len);
  void OnRequestComplete(
      int request_id,
      const URLRequestStatus& status,
      const std::string& security_info);

  // Dispatch the message to one of the message response handlers.
  void DispatchMessage(const IPC::Message& message);

  // Dispatch any deferred messages for the given request, provided it is not
  // again in the deferred state.
  void FlushDeferredMessages(int request_id);

  // Returns true if the message passed in is a resource related message.
  static bool IsResourceDispatcherMessage(const IPC::Message& message);

  // ViewHostMsg_Resource_DataReceived is not POD, it has a shared memory
  // handle in it that we should cleanup it up nicely. This method accepts any
  // message and determine whether the message is
  // ViewHostMsg_Resource_DataReceived and clean up the shared memory handle.
  static void ReleaseResourcesInDataMessage(const IPC::Message& message);

  // Iterate through a message queue and clean up the messages by calling
  // ReleaseResourcesInDataMessage and removing them from the queue. Intended
  // for use on deferred message queues that are no longer needed.
  static void ReleaseResourcesInMessageQueue(MessageQueue* queue);

  IPC::Message::Sender* message_sender_;

  // All pending requests issued to the host
  PendingRequestList pending_requests_;

  ScopedRunnableMethodFactory<ResourceDispatcher> method_factory_;

  DISALLOW_COPY_AND_ASSIGN(ResourceDispatcher);
};

#endif  // CHROME_COMMON_RESOURCE_DISPATCHER_H__
