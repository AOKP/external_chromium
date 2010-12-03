// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/gpu_messages.h"

#include "chrome/common/gpu_create_command_buffer_config.h"
#include "chrome/common/gpu_info.h"
#include "chrome/common/dx_diag_node.h"
#include "gfx/rect.h"
#include "gfx/size.h"
#include "ipc/ipc_channel_handle.h"
#include "ipc/ipc_message_utils.h"

#define MESSAGES_INTERNAL_IMPL_FILE \
  "chrome/common/gpu_messages_internal.h"
#include "ipc/ipc_message_impl_macros.h"

#if defined(OS_MACOSX)

// Parameters for the GpuHostMsg_AcceleratedSurfaceSetIOSurface
// message, which has too many parameters to be sent with the
// predefined IPC macros.
GpuHostMsg_AcceleratedSurfaceSetIOSurface_Params::
    GpuHostMsg_AcceleratedSurfaceSetIOSurface_Params()
        : renderer_id(0),
          render_view_id(0),
          window(NULL),
          width(0),
          height(0),
          identifier(0) {
}

#endif

namespace IPC {

#if defined(OS_MACOSX)

void ParamTraits<GpuHostMsg_AcceleratedSurfaceSetIOSurface_Params> ::Write(
    Message* m,
    const param_type& p) {
  WriteParam(m, p.renderer_id);
  WriteParam(m, p.render_view_id);
  WriteParam(m, p.window);
  WriteParam(m, p.width);
  WriteParam(m, p.height);
  WriteParam(m, p.identifier);
}

bool ParamTraits<GpuHostMsg_AcceleratedSurfaceSetIOSurface_Params> ::Read(
    const Message* m,
    void** iter,
    param_type* p) {
  return ReadParam(m, iter, &p->renderer_id) &&
      ReadParam(m, iter, &p->render_view_id) &&
      ReadParam(m, iter, &p->window) &&
      ReadParam(m, iter, &p->width) &&
      ReadParam(m, iter, &p->height) &&
      ReadParam(m, iter, &p->identifier);
}

void ParamTraits<GpuHostMsg_AcceleratedSurfaceSetIOSurface_Params> ::Log(
     const param_type& p,
     std::string* l) {
  l->append("(");
  LogParam(p.renderer_id, l);
  l->append(", ");
  LogParam(p.render_view_id, l);
  l->append(", ");
  LogParam(p.window, l);
  l->append(", ");
  LogParam(p.width, l);
  l->append(", ");
  LogParam(p.height, l);
  l->append(", ");
  LogParam(p.identifier, l);
  l->append(")");
}

#endif  // if defined(OS_MACOSX)

void ParamTraits<GPUInfo> ::Write(Message* m, const param_type& p) {
  WriteParam(m, static_cast<int32>(p.progress()));
  WriteParam(m, p.initialization_time());
  WriteParam(m, p.vendor_id());
  WriteParam(m, p.device_id());
  WriteParam(m, p.driver_version());
  WriteParam(m, p.pixel_shader_version());
  WriteParam(m, p.vertex_shader_version());
  WriteParam(m, p.gl_version());
  WriteParam(m, p.can_lose_context());

#if defined(OS_WIN)
  ParamTraits<DxDiagNode> ::Write(m, p.dx_diagnostics());
#endif
}

bool ParamTraits<GPUInfo> ::Read(const Message* m, void** iter, param_type* p) {
  int32 progress;
  base::TimeDelta initialization_time;
  uint32 vendor_id;
  uint32 device_id;
  std::wstring driver_version;
  uint32 pixel_shader_version;
  uint32 vertex_shader_version;
  uint32 gl_version;
  bool can_lose_context;
  bool ret = ReadParam(m, iter, &progress);
  ret = ret && ReadParam(m, iter, &initialization_time);
  ret = ret && ReadParam(m, iter, &vendor_id);
  ret = ret && ReadParam(m, iter, &device_id);
  ret = ret && ReadParam(m, iter, &driver_version);
  ret = ret && ReadParam(m, iter, &pixel_shader_version);
  ret = ret && ReadParam(m, iter, &vertex_shader_version);
  ret = ret && ReadParam(m, iter, &gl_version);
  ret = ret && ReadParam(m, iter, &can_lose_context);
  p->SetProgress(static_cast<GPUInfo::Progress>(progress));
  if (!ret)
    return false;

  p->SetInitializationTime(initialization_time);
  p->SetGraphicsInfo(vendor_id,
                     device_id,
                     driver_version,
                     pixel_shader_version,
                     vertex_shader_version,
                     gl_version,
                     can_lose_context);

#if defined(OS_WIN)
  DxDiagNode dx_diagnostics;
  if (!ReadParam(m, iter, &dx_diagnostics))
    return false;

  p->SetDxDiagnostics(dx_diagnostics);
#endif

  return true;
}

void ParamTraits<GPUInfo> ::Log(const param_type& p, std::string* l) {
  l->append(base::StringPrintf("<GPUInfo> %d %d %x %x %ls %d",
                               p.progress(),
                               static_cast<int32>(
                                   p.initialization_time().InMilliseconds()),
                               p.vendor_id(),
                               p.device_id(),
                               p.driver_version().c_str(),
                               p.can_lose_context()));
}

void ParamTraits<DxDiagNode> ::Write(Message* m, const param_type& p) {
  WriteParam(m, p.values);
  WriteParam(m, p.children);
}

bool ParamTraits<DxDiagNode> ::Read(const Message* m,
                                    void** iter,
                                    param_type* p) {
  bool ret = ReadParam(m, iter, &p->values);
  ret = ret && ReadParam(m, iter, &p->children);
  return ret;
}

void ParamTraits<DxDiagNode> ::Log(const param_type& p, std::string* l) {
  l->append("<DxDiagNode>");
}

void ParamTraits<gpu::CommandBuffer::State> ::Write(Message* m,
                                                    const param_type& p) {
  WriteParam(m, p.num_entries);
  WriteParam(m, p.get_offset);
  WriteParam(m, p.put_offset);
  WriteParam(m, p.token);
  WriteParam(m, static_cast<int32>(p.error));
}

bool ParamTraits<gpu::CommandBuffer::State> ::Read(const Message* m,
                                                   void** iter,
                                                   param_type* p) {
  int32 temp;
  if (ReadParam(m, iter, &p->num_entries) &&
      ReadParam(m, iter, &p->get_offset) &&
      ReadParam(m, iter, &p->put_offset) &&
      ReadParam(m, iter, &p->token) &&
      ReadParam(m, iter, &temp)) {
    p->error = static_cast<gpu::error::Error>(temp);
    return true;
  } else {
    return false;
  }
}

void ParamTraits<gpu::CommandBuffer::State> ::Log(const param_type& p,
                                                  std::string* l) {
  l->append("<CommandBuffer::State>");
}

void ParamTraits<GPUCreateCommandBufferConfig> ::Write(
    Message* m, const param_type& p) {
  WriteParam(m, p.allowed_extensions);
  WriteParam(m, p.attribs);
}

bool ParamTraits<GPUCreateCommandBufferConfig> ::Read(
    const Message* m, void** iter, param_type* p) {
  if (!ReadParam(m, iter, &p->allowed_extensions) ||
      !ReadParam(m, iter, &p->attribs)) {
    return false;
  }
  return true;
}

void ParamTraits<GPUCreateCommandBufferConfig> ::Log(
    const param_type& p, std::string* l) {
  l->append("<GPUCreateCommandBufferConfig>");
}

}  // namespace IPC
