// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/pepper_plugin_registry.h"

#include "base/command_line.h"
#include "base/file_util.h"
#include "base/native_library.h"
#include "base/path_service.h"
#include "base/string_split.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "remoting/client/plugin/pepper_entrypoints.h"

const char* PepperPluginRegistry::kPDFPluginName = "Chrome PDF Viewer";
const char* PepperPluginRegistry::kPDFPluginMimeType = "application/pdf";
const char* PepperPluginRegistry::kPDFPluginExtension = "pdf";
const char* PepperPluginRegistry::kPDFPluginDescription =
    "Portable Document Format";

PepperPluginInfo::PepperPluginInfo() : is_internal(false) {
}

PepperPluginInfo::~PepperPluginInfo() {}

// static
PepperPluginRegistry* PepperPluginRegistry::GetInstance() {
  static PepperPluginRegistry registry;
  return &registry;
}

// static
void PepperPluginRegistry::GetList(std::vector<PepperPluginInfo>* plugins) {
  InternalPluginInfoList internal_plugin_info;
  GetInternalPluginInfo(&internal_plugin_info);
  for (InternalPluginInfoList::const_iterator it =
         internal_plugin_info.begin();
       it != internal_plugin_info.end();
       ++it) {
    plugins->push_back(*it);
  }

  GetPluginInfoFromSwitch(plugins);
  GetExtraPlugins(plugins);
}

// static
void PepperPluginRegistry::PreloadModules() {
  std::vector<PepperPluginInfo> plugins;
  GetList(&plugins);
  for (size_t i = 0; i < plugins.size(); ++i) {
    if (!plugins[i].is_internal) {
      base::NativeLibrary library = base::LoadNativeLibrary(plugins[i].path);
      LOG_IF(WARNING, !library) << "Unable to load plugin "
                                << plugins[i].path.value();
    }
  }
}

// static
void PepperPluginRegistry::GetPluginInfoFromSwitch(
    std::vector<PepperPluginInfo>* plugins) {
  const std::string value =
      CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          switches::kRegisterPepperPlugins);
  if (value.empty())
    return;

  // FORMAT:
  // command-line = <plugin-entry> + *( LWS + "," + LWS + <plugin-entry> )
  // plugin-entry = <file-path> + ["#" + <name> + ["#" + <description>]] +
  //                *1( LWS + ";" + LWS + <mime-type> )

  std::vector<std::string> modules;
  base::SplitString(value, ',', &modules);
  for (size_t i = 0; i < modules.size(); ++i) {
    std::vector<std::string> parts;
    base::SplitString(modules[i], ';', &parts);
    if (parts.size() < 2) {
      DLOG(ERROR) << "Required mime-type not found";
      continue;
    }

    std::vector<std::string> name_parts;
    base::SplitString(parts[0], '#', &name_parts);

    PepperPluginInfo plugin;
#if defined(OS_WIN)
    // This means we can't provide plugins from non-ASCII paths, but
    // since this switch is only for development I don't think that's
    // too awful.
    plugin.path = FilePath(ASCIIToUTF16(name_parts[0]));
#else
    plugin.path = FilePath(name_parts[0]);
#endif
    if (name_parts.size() > 1)
      plugin.name = name_parts[1];
    if (name_parts.size() > 2)
      plugin.type_descriptions = name_parts[2];
    for (size_t j = 1; j < parts.size(); ++j)
      plugin.mime_types.push_back(parts[j]);

    plugins->push_back(plugin);
  }
}

// static
void PepperPluginRegistry::GetExtraPlugins(
    std::vector<PepperPluginInfo>* plugins) {
  // Once we're sandboxed, we can't know if the PDF plugin is
  // available or not; but (on Linux) this function is always called
  // once before we're sandboxed.  So the first time through test if
  // the file is available and then skip the check on subsequent calls
  // if yes.
  static bool skip_pdf_file_check = false;
  FilePath path;
  if (PathService::Get(chrome::FILE_PDF_PLUGIN, &path)) {
    if (skip_pdf_file_check || file_util::PathExists(path)) {
      PepperPluginInfo pdf;
      pdf.path = path;
      pdf.name = kPDFPluginName;
      pdf.mime_types.push_back(kPDFPluginMimeType);
      pdf.file_extensions = kPDFPluginExtension;
      pdf.type_descriptions = kPDFPluginDescription;
      plugins->push_back(pdf);

      skip_pdf_file_check = true;
    }
  }
}

PepperPluginRegistry::InternalPluginInfo::InternalPluginInfo() {
  is_internal = true;
}

// static
void PepperPluginRegistry::GetInternalPluginInfo(
    InternalPluginInfoList* plugin_info) {
  // Currently, to centralize the internal plugin registration logic, we
  // hardcode the list of plugins, mimetypes, and registration information
  // in this function.  This is gross, but because the GetList() function is
  // called from both the renderer and browser the other option is to force a
  // special register function for each plugin to be called by both
  // RendererMain() and BrowserMain().  This seemed like the better tradeoff.
  //
  // TODO(ajwong): Think up a better way to maintain the plugin registration
  // information. Pehraps by construction of a singly linked list of
  // plugin initializers that is built with static initializers?

#if defined(ENABLE_REMOTING)
  if (CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kEnableRemoting)) {
    InternalPluginInfo info;
    // Add the chromoting plugin.
    DCHECK(info.is_internal);
    info.path =
        FilePath(FILE_PATH_LITERAL("internal-chromoting"));
    info.mime_types.push_back("pepper-application/x-chromoting");
    info.entry_points.get_interface = remoting::PPP_GetInterface;
    info.entry_points.initialize_module = remoting::PPP_InitializeModule;
    info.entry_points.shutdown_module = remoting::PPP_ShutdownModule;

    plugin_info->push_back(info);
  }
#endif
}

pepper::PluginModule* PepperPluginRegistry::GetModule(
    const FilePath& path) const {
  ModuleMap::const_iterator it = modules_.find(path);
  if (it == modules_.end())
    return NULL;
  return it->second;
}

PepperPluginRegistry::~PepperPluginRegistry() {}

PepperPluginRegistry::PepperPluginRegistry() {
  InternalPluginInfoList internal_plugin_info;
  GetInternalPluginInfo(&internal_plugin_info);
  // Register modules for these suckers.
  for (InternalPluginInfoList::const_iterator it =
         internal_plugin_info.begin();
       it != internal_plugin_info.end();
       ++it) {
    const FilePath& path = it->path;
    ModuleHandle module =
        pepper::PluginModule::CreateInternalModule(it->entry_points);
    if (!module) {
      DLOG(ERROR) << "Failed to load pepper module: " << path.value();
      continue;
    }
    module->set_name(it->name);
    modules_[path] = module;
  }

  // Add the modules specified on the command line last so that they can
  // override the internal plugins.
  std::vector<PepperPluginInfo> plugins;
  GetPluginInfoFromSwitch(&plugins);
  GetExtraPlugins(&plugins);
  for (size_t i = 0; i < plugins.size(); ++i) {
    const FilePath& path = plugins[i].path;
    ModuleHandle module = pepper::PluginModule::CreateModule(path);
    if (!module) {
      DLOG(ERROR) << "Failed to load pepper module: " << path.value();
      continue;
    }
    module->set_name(plugins[i].name);
    modules_[path] = module;
  }
}
