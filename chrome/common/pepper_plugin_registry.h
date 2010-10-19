// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_COMMON_PEPPER_PLUGIN_REGISTRY_H_
#define CHROME_COMMON_PEPPER_PLUGIN_REGISTRY_H_
#pragma once

#include <map>
#include <string>
#include <vector>

#include "base/file_path.h"
#include "webkit/glue/plugins/pepper_plugin_module.h"

struct PepperPluginInfo {
  PepperPluginInfo();  // Needed to initialize |is_internal|.

  bool is_internal;  // Defaults to false (see constructor).
  FilePath path;  // Internal plugins have "internal-[name]" as path.
  std::vector<std::string> mime_types;
  std::string name;
  std::string description;
  std::string file_extensions;
  std::string type_descriptions;
};

// This class holds references to all of the known pepper plugin modules.
class PepperPluginRegistry {
 public:
  static PepperPluginRegistry* GetInstance();

  // Returns the list of known pepper plugins.  This method is static so that
  // it can be used by the browser process, which has no need to load the
  // pepper plugin modules.
  static void GetList(std::vector<PepperPluginInfo>* plugins);

  // Loads the (native) libraries but does not initialize them (i.e., does not
  // call PPP_InitializeModule). This is needed by the zygote on Linux to get
  // access to the plugins before entering the sandbox.
  static void PreloadModules();

  pepper::PluginModule* GetModule(const FilePath& path) const;

 private:
  static void GetPluginInfoFromSwitch(std::vector<PepperPluginInfo>* plugins);
  static void GetExtraPlugins(std::vector<PepperPluginInfo>* plugins);

  struct InternalPluginInfo : public PepperPluginInfo {
    InternalPluginInfo();  // Sets |is_internal|.
    pepper::PluginModule::EntryPoints entry_points;
  };
  typedef std::vector<InternalPluginInfo> InternalPluginInfoList;
  static void GetInternalPluginInfo(InternalPluginInfoList* plugin_info);

  PepperPluginRegistry();

  typedef scoped_refptr<pepper::PluginModule> ModuleHandle;
  typedef std::map<FilePath, ModuleHandle> ModuleMap;
  ModuleMap modules_;
};

#endif  // CHROME_COMMON_PEPPER_PLUGIN_REGISTRY_H_
