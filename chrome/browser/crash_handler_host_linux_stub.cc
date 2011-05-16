// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a stub file which is compiled in when we are building without
// breakpad support.

#include "chrome/browser/crash_handler_host_linux.h"

#include "base/singleton.h"

CrashHandlerHostLinux::CrashHandlerHostLinux()
    : process_socket_(-1),
      browser_socket_(-1) {
}

CrashHandlerHostLinux::~CrashHandlerHostLinux() {
}

void CrashHandlerHostLinux::OnFileCanReadWithoutBlocking(int fd) {
}

void CrashHandlerHostLinux::OnFileCanWriteWithoutBlocking(int fd) {
}

void CrashHandlerHostLinux::WillDestroyCurrentMessageLoop() {
}

PluginCrashHandlerHostLinux::PluginCrashHandlerHostLinux() {
}

PluginCrashHandlerHostLinux::~PluginCrashHandlerHostLinux() {
}

// static
PluginCrashHandlerHostLinux* PluginCrashHandlerHostLinux::GetInstance() {
  return Singleton<PluginCrashHandlerHostLinux>::get();
}

RendererCrashHandlerHostLinux::RendererCrashHandlerHostLinux() {
}

RendererCrashHandlerHostLinux::~RendererCrashHandlerHostLinux() {
}

// static
RendererCrashHandlerHostLinux* RendererCrashHandlerHostLinux::GetInstance() {
  return Singleton<RendererCrashHandlerHostLinux>::get();
}
