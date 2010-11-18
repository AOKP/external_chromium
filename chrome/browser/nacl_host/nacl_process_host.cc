// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "build/build_config.h"

#include "chrome/browser/nacl_host/nacl_process_host.h"

#if defined(OS_POSIX)
#include <fcntl.h>
#endif

#include "base/command_line.h"
#include "base/metrics/nacl_histogram.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/renderer_host/resource_message_filter.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/logging_chrome.h"
#include "chrome/common/nacl_cmd_line.h"
#include "chrome/common/nacl_messages.h"
#include "chrome/common/render_messages.h"
#include "ipc/ipc_switches.h"

#if defined(OS_POSIX)
#include "ipc/ipc_channel_posix.h"
#elif defined(OS_WIN)
#include "chrome/browser/nacl_host/nacl_broker_service_win.h"
#endif

namespace {

#if !defined(DISABLE_NACL)
void SetCloseOnExec(nacl::Handle fd) {
#if defined(OS_POSIX)
  int flags = fcntl(fd, F_GETFD);
  CHECK(flags != -1);
  int rc = fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
  CHECK(rc == 0);
#endif
}
#endif

}  // namespace

NaClProcessHost::NaClProcessHost(
    ResourceDispatcherHost *resource_dispatcher_host,
    const std::wstring& url)
    : BrowserChildProcessHost(NACL_LOADER_PROCESS, resource_dispatcher_host),
      resource_dispatcher_host_(resource_dispatcher_host),
      reply_msg_(NULL),
      running_on_wow64_(false) {
  set_name(url);
#if defined(OS_WIN)
  CheckIsWow64();
#endif
}

NaClProcessHost::~NaClProcessHost() {
  if (!reply_msg_)
    return;

  // nacl::Close() is not available at link time if DISABLE_NACL is
  // defined, but we still compile a bunch of other code from this
  // file anyway.  TODO(mseaborn): Make this less messy.
#ifndef DISABLE_NACL
  for (size_t i = 0; i < sockets_for_renderer_.size(); i++) {
    nacl::Close(sockets_for_renderer_[i]);
  }
  for (size_t i = 0; i < sockets_for_sel_ldr_.size(); i++) {
    nacl::Close(sockets_for_sel_ldr_[i]);
  }
#endif

  // OnProcessLaunched didn't get called because the process couldn't launch.
  // Don't keep the renderer hanging.
  reply_msg_->set_reply_error();
  resource_message_filter_->Send(reply_msg_);
}

bool NaClProcessHost::Launch(ResourceMessageFilter* resource_message_filter,
                             int socket_count,
                             IPC::Message* reply_msg) {
#ifdef DISABLE_NACL
  NOTIMPLEMENTED() << "Native Client disabled at build time";
  return false;
#else
  // Place an arbitrary limit on the number of sockets to limit
  // exposure in case the renderer is compromised.  We can increase
  // this if necessary.
  if (socket_count > 8) {
    return false;
  }

  // Rather than creating a socket pair in the renderer, and passing
  // one side through the browser to sel_ldr, socket pairs are created
  // in the browser and then passed to the renderer and sel_ldr.
  //
  // This is mainly for the benefit of Windows, where sockets cannot
  // be passed in messages, but are copied via DuplicateHandle().
  // This means the sandboxed renderer cannot send handles to the
  // browser process.

  for (int i = 0; i < socket_count; i++) {
    nacl::Handle pair[2];
    // Create a connected socket
    if (nacl::SocketPair(pair) == -1)
      return false;
    sockets_for_renderer_.push_back(pair[0]);
    sockets_for_sel_ldr_.push_back(pair[1]);
    SetCloseOnExec(pair[0]);
    SetCloseOnExec(pair[1]);
  }

  // Launch the process
  if (!LaunchSelLdr()) {
    return false;
  }
  UmaNaclHistogramEnumeration(NACL_STARTED);
  resource_message_filter_ = resource_message_filter;
  reply_msg_ = reply_msg;

  return true;
#endif  // DISABLE_NACL
}

bool NaClProcessHost::LaunchSelLdr() {
  if (!CreateChannel())
    return false;

  // Build command line for nacl.
  FilePath exe_path = GetChildPath(true);
  if (exe_path.empty())
    return false;

  CommandLine* cmd_line = new CommandLine(exe_path);
  nacl::CopyNaClCommandLineArguments(cmd_line);

  cmd_line->AppendSwitchASCII(switches::kProcessType,
                              switches::kNaClLoaderProcess);

  cmd_line->AppendSwitchASCII(switches::kProcessChannelID, channel_id());

  // On Windows we might need to start the broker process to launch a new loader
#if defined(OS_WIN)
  if (running_on_wow64_) {
    NaClBrokerService::GetInstance()->Init(resource_dispatcher_host_);
    return NaClBrokerService::GetInstance()->LaunchLoader(this,
        ASCIIToWide(channel_id()));
  } else {
    BrowserChildProcessHost::Launch(FilePath(), cmd_line);
  }
#elif defined(OS_POSIX)
  BrowserChildProcessHost::Launch(true,  // use_zygote
                                  base::environment_vector(),
                                  cmd_line);
#endif

  return true;
}

void NaClProcessHost::OnProcessLaunchedByBroker(base::ProcessHandle handle) {
  set_handle(handle);
  OnProcessLaunched();
}

bool NaClProcessHost::DidChildCrash() {
  if (running_on_wow64_)
    return base::DidProcessCrash(NULL, handle());
  return BrowserChildProcessHost::DidChildCrash();
}

void NaClProcessHost::OnChildDied() {
#if defined(OS_WIN)
  NaClBrokerService::GetInstance()->OnLoaderDied();
#endif
  BrowserChildProcessHost::OnChildDied();
}

void NaClProcessHost::OnProcessLaunched() {
  std::vector<nacl::FileDescriptor> handles_for_renderer;
  base::ProcessHandle nacl_process_handle;

  for (size_t i = 0; i < sockets_for_renderer_.size(); i++) {
#if defined(OS_WIN)
    // Copy the handle into the renderer process.
    HANDLE handle_in_renderer;
    DuplicateHandle(base::GetCurrentProcessHandle(),
                    reinterpret_cast<HANDLE>(sockets_for_renderer_[i]),
                    resource_message_filter_->handle(),
                    &handle_in_renderer,
                    GENERIC_READ | GENERIC_WRITE,
                    FALSE,
                    DUPLICATE_CLOSE_SOURCE);
    handles_for_renderer.push_back(
        reinterpret_cast<nacl::FileDescriptor>(handle_in_renderer));
#else
    // No need to dup the imc_handle - we don't pass it anywhere else so
    // it cannot be closed.
    nacl::FileDescriptor imc_handle;
    imc_handle.fd = sockets_for_renderer_[i];
    imc_handle.auto_close = true;
    handles_for_renderer.push_back(imc_handle);
#endif
  }

#if defined(OS_WIN)
  // Copy the process handle into the renderer process.
  DuplicateHandle(base::GetCurrentProcessHandle(),
                  handle(),
                  resource_message_filter_->handle(),
                  &nacl_process_handle,
                  PROCESS_DUP_HANDLE,
                  FALSE,
                  0);
#else
  // We use pid as process handle on Posix
  nacl_process_handle = handle();
#endif

  // Get the pid of the NaCl process
  base::ProcessId nacl_process_id = base::GetProcId(handle());

  ViewHostMsg_LaunchNaCl::WriteReplyParams(
      reply_msg_, handles_for_renderer, nacl_process_handle, nacl_process_id);
  resource_message_filter_->Send(reply_msg_);
  resource_message_filter_ = NULL;
  reply_msg_ = NULL;
  sockets_for_renderer_.clear();

  SendStartMessage();
}

void NaClProcessHost::SendStartMessage() {
  std::vector<nacl::FileDescriptor> handles_for_sel_ldr;
  for (size_t i = 0; i < sockets_for_sel_ldr_.size(); i++) {
#if defined(OS_WIN)
    HANDLE channel;
    if (!DuplicateHandle(GetCurrentProcess(),
                         reinterpret_cast<HANDLE>(sockets_for_sel_ldr_[i]),
                         handle(),
                         &channel,
                         GENERIC_READ | GENERIC_WRITE,
                         FALSE, DUPLICATE_CLOSE_SOURCE)) {
      return;
    }
    handles_for_sel_ldr.push_back(
        reinterpret_cast<nacl::FileDescriptor>(channel));
#else
    nacl::FileDescriptor channel;
    channel.fd = dup(sockets_for_sel_ldr_[i]);
    channel.auto_close = true;
    handles_for_sel_ldr.push_back(channel);
#endif
  }

  Send(new NaClProcessMsg_Start(handles_for_sel_ldr));
  sockets_for_sel_ldr_.clear();
}

void NaClProcessHost::OnMessageReceived(const IPC::Message& msg) {
  NOTREACHED() << "Invalid message with type = " << msg.type();
}

URLRequestContext* NaClProcessHost::GetRequestContext(
    uint32 request_id,
    const ViewHostMsg_Resource_Request& request_data) {
  return NULL;
}

#if defined(OS_WIN)
// TODO(gregoryd): invoke CheckIsWow64 only once, not for each NaClProcessHost
typedef BOOL (WINAPI *LPFN_ISWOW64PROCESS) (HANDLE, PBOOL);
void NaClProcessHost::CheckIsWow64() {
  LPFN_ISWOW64PROCESS fnIsWow64Process;

  fnIsWow64Process = (LPFN_ISWOW64PROCESS) GetProcAddress(
      GetModuleHandle(TEXT("kernel32")),
      "IsWow64Process");

  if (fnIsWow64Process != NULL) {
    BOOL bIsWow64 = FALSE;
    if (fnIsWow64Process(GetCurrentProcess(),&bIsWow64)) {
      if (bIsWow64) {
        running_on_wow64_ = true;
      }
    }
  }
}
#endif
