// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/gpu_info.h"

GPUInfo::GPUInfo()
    : level_(kUninitialized),
      vendor_id_(0),
      device_id_(0),
      driver_vendor_(""),
      driver_version_(""),
      driver_date_(""),
      pixel_shader_version_(0),
      vertex_shader_version_(0),
      gl_version_(0),
      gl_version_string_(""),
      gl_vendor_(""),
      gl_renderer_(""),
      gl_extensions_(""),
      can_lose_context_(false),
      collection_error_(false) {
}

GPUInfo::~GPUInfo() {}

GPUInfo::Level GPUInfo::level() const {
  return level_;
}

base::TimeDelta GPUInfo::initialization_time() const {
  return initialization_time_;
}

uint32 GPUInfo::vendor_id() const {
  return vendor_id_;
}

uint32 GPUInfo::device_id() const {
  return device_id_;
}

std::string GPUInfo::driver_vendor() const {
  return driver_vendor_;
}

std::string GPUInfo::driver_version() const {
  return driver_version_;
}

std::string GPUInfo::driver_date() const {
  return driver_date_;
}

uint32 GPUInfo::pixel_shader_version() const {
  return pixel_shader_version_;
}

uint32 GPUInfo::vertex_shader_version() const {
  return vertex_shader_version_;
}

uint32 GPUInfo::gl_version() const {
  return gl_version_;
}

std::string GPUInfo::gl_version_string() const {
  return gl_version_string_;
}

std::string GPUInfo::gl_vendor() const {
  return gl_vendor_;
}

std::string GPUInfo::gl_renderer() const {
  return gl_renderer_;
}

std::string GPUInfo::gl_extensions() const {
  return gl_extensions_;
}

bool GPUInfo::can_lose_context() const {
  return can_lose_context_;
}

bool GPUInfo::collection_error() const {
  return collection_error_;
}

void GPUInfo::SetLevel(Level level) {
  level_ = level;
}

void GPUInfo::SetInitializationTime(
    const base::TimeDelta& initialization_time) {
  initialization_time_ = initialization_time;
}

void GPUInfo::SetVideoCardInfo(uint32 vendor_id, uint32 device_id) {
  vendor_id_ = vendor_id;
  device_id_ = device_id;
}

void GPUInfo::SetDriverInfo(const std::string& driver_vendor,
                            const std::string& driver_version,
                            const std::string& driver_date) {
  if (driver_vendor.length() > 0)
    driver_vendor_ = driver_vendor;
  if (driver_version.length() > 0)
    driver_version_ = driver_version;
  if (driver_date.length() > 0)
    driver_date_ = driver_date;
}

void GPUInfo::SetShaderVersion(uint32 pixel_shader_version,
                               uint32 vertex_shader_version) {
  pixel_shader_version_ = pixel_shader_version;
  vertex_shader_version_ = vertex_shader_version;
}

void GPUInfo::SetGLVersion(uint32 gl_version) {
  gl_version_ = gl_version;
}

void GPUInfo::SetGLVersionString(const std::string& gl_version_string) {
  gl_version_string_ = gl_version_string;
}

void GPUInfo::SetGLVendor(const std::string& gl_vendor) {
  gl_vendor_ = gl_vendor;
}

void GPUInfo::SetGLRenderer(const std::string& gl_renderer) {
  gl_renderer_ = gl_renderer;
}

void GPUInfo::SetGLExtensions(const std::string& gl_extensions) {
  gl_extensions_ = gl_extensions;
}

void GPUInfo::SetCanLoseContext(bool can_lose_context) {
  can_lose_context_ = can_lose_context;
}

void GPUInfo::SetCollectionError(bool collection_error) {
  collection_error_ = collection_error;
}

#if defined(OS_WIN)
const DxDiagNode& GPUInfo::dx_diagnostics() const {
  return dx_diagnostics_;
}

void GPUInfo::SetDxDiagnostics(const DxDiagNode& dx_diagnostics) {
  dx_diagnostics_ = dx_diagnostics;
}
#endif
