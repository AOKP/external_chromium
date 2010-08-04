// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/debugger/devtools_protocol_handler.h"

#include "base/logging.h"
#include "chrome/browser/chrome_thread.h"
#include "chrome/browser/debugger/inspectable_tab_proxy.h"
#include "chrome/browser/debugger/devtools_remote_message.h"
#include "chrome/browser/debugger/devtools_remote_listen_socket.h"
#include "chrome/browser/tab_contents/tab_contents.h"

DevToolsProtocolHandler::DevToolsProtocolHandler(int port)
    : port_(port),
      connection_(NULL),
      server_(NULL) {
  inspectable_tab_proxy_.reset(new InspectableTabProxy);
}

DevToolsProtocolHandler::~DevToolsProtocolHandler() {
  // Stop() must be called prior to this being called
  DCHECK(server_.get() == NULL);
  DCHECK(connection_.get() == NULL);
}

void DevToolsProtocolHandler::Start() {
  ChromeThread::PostTask(
      ChromeThread::IO, FROM_HERE,
      NewRunnableMethod(this, &DevToolsProtocolHandler::Init));
}

void DevToolsProtocolHandler::Init() {
  server_ = DevToolsRemoteListenSocket::Listen(
      "127.0.0.1", port_, this);
}

void DevToolsProtocolHandler::Stop() {
  ChromeThread::PostTask(
      ChromeThread::IO, FROM_HERE,
      NewRunnableMethod(this, &DevToolsProtocolHandler::Teardown));
  tool_to_listener_map_.clear();  // Releases all scoped_refptr's to listeners
}

// Run in I/O thread
void DevToolsProtocolHandler::Teardown() {
  connection_ = NULL;
  server_ = NULL;
}

void DevToolsProtocolHandler::RegisterDestination(
    DevToolsRemoteListener* listener,
    const std::string& tool_name) {
  DCHECK(tool_to_listener_map_.find(tool_name) == tool_to_listener_map_.end());
  tool_to_listener_map_.insert(std::make_pair(tool_name, listener));
}

void DevToolsProtocolHandler::UnregisterDestination(
    DevToolsRemoteListener* listener,
    const std::string& tool_name) {
  DCHECK(tool_to_listener_map_.find(tool_name) != tool_to_listener_map_.end());
  DCHECK(tool_to_listener_map_.find(tool_name)->second == listener);
  tool_to_listener_map_.erase(tool_name);
}

void DevToolsProtocolHandler::HandleMessage(
    const DevToolsRemoteMessage& message) {
  std::string tool = message.GetHeaderWithEmptyDefault(
      DevToolsRemoteMessageHeaders::kTool);
  ToolToListenerMap::const_iterator it = tool_to_listener_map_.find(tool);
  if (it == tool_to_listener_map_.end()) {
    NOTREACHED();  // an unsupported tool, bail out
    return;
  }
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::IO));
  ChromeThread::PostTask(
      ChromeThread::UI, FROM_HERE,
      NewRunnableMethod(
          it->second.get(), &DevToolsRemoteListener::HandleMessage, message));
}

void DevToolsProtocolHandler::Send(const DevToolsRemoteMessage& message) {
  if (connection_ != NULL) {
    connection_->Send(message.ToString());
  }
}

void DevToolsProtocolHandler::OnAcceptConnection(ListenSocket *connection) {
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::IO));
  connection_ = connection;
}

void DevToolsProtocolHandler::OnConnectionLost() {
  DCHECK(ChromeThread::CurrentlyOn(ChromeThread::IO));
  connection_ = NULL;
  for (ToolToListenerMap::const_iterator it = tool_to_listener_map_.begin(),
       end = tool_to_listener_map_.end();
       it != end;
       ++it) {
    ChromeThread::PostTask(
      ChromeThread::UI, FROM_HERE,
      NewRunnableMethod(
          it->second.get(), &DevToolsRemoteListener::OnConnectionLost));
  }
}
