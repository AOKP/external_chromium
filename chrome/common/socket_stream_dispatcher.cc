// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/socket_stream_dispatcher.h"

#include <vector>

#include "base/id_map.h"
#include "base/message_loop.h"
#include "base/ref_counted.h"
#include "base/task.h"
#include "chrome/common/child_thread.h"
#include "chrome/common/render_messages.h"
#include "chrome/common/net/socket_stream.h"
#include "googleurl/src/gurl.h"
#include "ipc/ipc_message.h"
#include "webkit/glue/websocketstreamhandle_bridge.h"
#include "webkit/glue/websocketstreamhandle_delegate.h"

// IPCWebSocketStreamHandleBridge is owned by each SocketStreamHandle.
// It communicates with the main browser process via SocketStreamDispatcher.
class IPCWebSocketStreamHandleBridge
    : public webkit_glue::WebSocketStreamHandleBridge {
 public:
  IPCWebSocketStreamHandleBridge(
      ChildThread* child_thread,
      WebKit::WebSocketStreamHandle* handle,
      webkit_glue::WebSocketStreamHandleDelegate* delegate)
      : socket_id_(chrome_common_net::kNoSocketId),
        child_thread_(child_thread),
        handle_(handle),
        delegate_(delegate) {}

  // Returns the handle having given id or NULL if there is no such handle.
  static IPCWebSocketStreamHandleBridge* FromSocketId(int id);

  // webkit_glue::WebSocketStreamHandleBridge methods.
  virtual void Connect(const GURL& url);
  virtual bool Send(const std::vector<char>& data);
  virtual void Close();

  // Called by SocketStreamDispatcher.
  void OnConnected(int max_amount_send_allowed);
  void OnSentData(int amount_sent);
  void OnReceivedData(const std::vector<char>& data);
  void OnClosed();

 private:
  virtual ~IPCWebSocketStreamHandleBridge();

  void DoConnect(const GURL& url);
  int socket_id_;

  ChildThread* child_thread_;
  WebKit::WebSocketStreamHandle* handle_;
  webkit_glue::WebSocketStreamHandleDelegate* delegate_;

  static IDMap<IPCWebSocketStreamHandleBridge> all_bridges;
};

IDMap<IPCWebSocketStreamHandleBridge>
IPCWebSocketStreamHandleBridge::all_bridges;

/* static */
IPCWebSocketStreamHandleBridge* IPCWebSocketStreamHandleBridge::FromSocketId(
    int id) {
  return all_bridges.Lookup(id);
}

IPCWebSocketStreamHandleBridge::~IPCWebSocketStreamHandleBridge() {
  DLOG(INFO) << "IPCWebSocketStreamHandleBridge destructor socket_id="
             << socket_id_;
  if (socket_id_ != chrome_common_net::kNoSocketId) {
    child_thread_->Send(new ViewHostMsg_Close(socket_id_));
    socket_id_ = chrome_common_net::kNoSocketId;
  }
}

void IPCWebSocketStreamHandleBridge::Connect(const GURL& url) {
  DCHECK(child_thread_);
  DLOG(INFO) << "Connect url=" << url;
  child_thread_->message_loop()->PostTask(
      FROM_HERE,
      NewRunnableMethod(this, &IPCWebSocketStreamHandleBridge::DoConnect,
                        url));
}

bool IPCWebSocketStreamHandleBridge::Send(
    const std::vector<char>& data) {
  DLOG(INFO) << "Send data.size=" << data.size();
  if (child_thread_->Send(
          new ViewHostMsg_SocketStream_SendData(socket_id_, data))) {
    if (delegate_)
      delegate_->WillSendData(handle_, &data[0], data.size());
    return true;
  }
  return false;
}

void IPCWebSocketStreamHandleBridge::Close() {
  DLOG(INFO) << "Close socket_id" << socket_id_;
  child_thread_->Send(new ViewHostMsg_SocketStream_Close(socket_id_));
}

void IPCWebSocketStreamHandleBridge::OnConnected(int max_pending_send_allowed) {
  DLOG(INFO) << "IPCWebSocketStreamHandleBridge::OnConnected socket_id="
             << socket_id_;
  if (delegate_)
    delegate_->DidOpenStream(handle_, max_pending_send_allowed);
}

void IPCWebSocketStreamHandleBridge::OnSentData(int amount_sent) {
  if (delegate_)
    delegate_->DidSendData(handle_, amount_sent);
}

void IPCWebSocketStreamHandleBridge::OnReceivedData(
    const std::vector<char>& data) {
  if (delegate_)
    delegate_->DidReceiveData(handle_, &data[0], data.size());
}

void IPCWebSocketStreamHandleBridge::OnClosed() {
  DLOG(INFO) << "IPCWebSocketStreamHandleBridge::OnClosed";
  if (socket_id_ != chrome_common_net::kNoSocketId) {
    all_bridges.Remove(socket_id_);
    socket_id_ = chrome_common_net::kNoSocketId;
  }
  if (delegate_) {
    delegate_->DidClose(handle_);
  }
  delegate_ = NULL;
  Release();
}

void IPCWebSocketStreamHandleBridge::DoConnect(const GURL& url) {
  DCHECK(child_thread_);
  DCHECK_EQ(socket_id_, chrome_common_net::kNoSocketId);
  if (delegate_)
    delegate_->WillOpenStream(handle_, url);

  socket_id_ = all_bridges.Add(this);
  DCHECK_NE(socket_id_, chrome_common_net::kNoSocketId);
  if (child_thread_->Send(
          new ViewHostMsg_SocketStream_Connect(url, socket_id_))) {
    DLOG(INFO) << "Connect socket_id=" << socket_id_;
    AddRef();  // Released in OnClosed().
    // TODO(ukai): timeout to OnConnected.
  } else {
    LOG(ERROR) << "IPC SocketStream_Connect failed.";
    OnClosed();
  }
}

SocketStreamDispatcher::SocketStreamDispatcher() {
}

/* static */
webkit_glue::WebSocketStreamHandleBridge*
SocketStreamDispatcher::CreateBridge(
    WebKit::WebSocketStreamHandle* handle,
    webkit_glue::WebSocketStreamHandleDelegate* delegate) {
  return new IPCWebSocketStreamHandleBridge(
      ChildThread::current(), handle, delegate);
}

bool SocketStreamDispatcher::OnMessageReceived(const IPC::Message& msg) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(SocketStreamDispatcher, msg)
    IPC_MESSAGE_HANDLER(ViewMsg_SocketStream_Connected, OnConnected)
    IPC_MESSAGE_HANDLER(ViewMsg_SocketStream_SentData, OnSentData)
    IPC_MESSAGE_HANDLER(ViewMsg_SocketStream_ReceivedData, OnReceivedData)
    IPC_MESSAGE_HANDLER(ViewMsg_SocketStream_Closed, OnClosed)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void SocketStreamDispatcher::OnConnected(int socket_id,
                                         int max_pending_send_allowed) {
  DLOG(INFO) << "SocketStreamDispatcher::OnConnected socket_id=" << socket_id
             << " max_pending_send_allowed=" << max_pending_send_allowed;
  IPCWebSocketStreamHandleBridge* bridge =
      IPCWebSocketStreamHandleBridge::FromSocketId(socket_id);
  if (bridge)
    bridge->OnConnected(max_pending_send_allowed);
  else
    DLOG(ERROR) << "No SocketStreamHandleBridge for socket_id=" << socket_id;
}

void SocketStreamDispatcher::OnSentData(int socket_id, int amount_sent) {
  IPCWebSocketStreamHandleBridge* bridge =
      IPCWebSocketStreamHandleBridge::FromSocketId(socket_id);
  if (bridge)
    bridge->OnSentData(amount_sent);
  else
    DLOG(ERROR) << "No SocketStreamHandleBridge for socket_id=" << socket_id;
}

void SocketStreamDispatcher::OnReceivedData(
    int socket_id, const std::vector<char>& data) {
  IPCWebSocketStreamHandleBridge* bridge =
      IPCWebSocketStreamHandleBridge::FromSocketId(socket_id);
  if (bridge)
    bridge->OnReceivedData(data);
  else
    DLOG(ERROR) << "No SocketStreamHandleBridge for socket_id=" << socket_id;
}

void SocketStreamDispatcher::OnClosed(int socket_id) {
  IPCWebSocketStreamHandleBridge* bridge =
      IPCWebSocketStreamHandleBridge::FromSocketId(socket_id);
  if (bridge)
    bridge->OnClosed();
  else
    DLOG(ERROR) << "No SocketStreamHandleBridge for socket_id=" << socket_id;
}
