// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/sandbox_policy.h"

#include <string>

#include "app/win_util.h"
#include "base/command_line.h"
#include "base/debug_util.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/process_util.h"
#include "base/stringprintf.h"
#include "base/string_number_conversions.h"
#include "base/string_util.h"
#include "base/trace_event.h"
#include "base/win_util.h"
#include "chrome/common/child_process_info.h"
#include "chrome/common/chrome_constants.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/debug_flags.h"
#include "sandbox/src/sandbox.h"

static sandbox::BrokerServices* g_broker_services = NULL;

namespace {

// The DLLs listed here are known (or under strong suspicion) of causing crashes
// when they are loaded in the renderer.
const wchar_t* const kTroublesomeDlls[] = {
  L"adialhk.dll",                 // Kaspersky Internet Security.
  L"acpiz.dll",                   // Unknown.
  L"avgrsstx.dll",                // AVG 8.
  L"btkeyind.dll",                // Widcomm Bluetooth.
  L"cmcsyshk.dll",                // CMC Internet Security.
  L"dockshellhook.dll",           // Stardock Objectdock.
  L"GoogleDesktopNetwork3.DLL",   // Google Desktop Search v5.
  L"fwhook.dll",                  // PC Tools Firewall Plus.
  L"hookprocesscreation.dll",     // Blumentals Program protector.
  L"hookterminateapis.dll",       // Blumentals and Cyberprinter.
  L"hookprintapis.dll",           // Cyberprinter.
  L"imon.dll",                    // NOD32 Antivirus.
  L"ioloHL.dll",                  // Iolo (System Mechanic).
  L"kloehk.dll",                  // Kaspersky Internet Security.
  L"lawenforcer.dll",             // Spyware-Browser AntiSpyware (Spybro).
  L"libdivx.dll",                 // DivX.
  L"lvprcinj01.dll",              // Logitech QuickCam.
  L"madchook.dll",                // Madshi (generic hooking library).
  L"mdnsnsp.dll",                 // Bonjour.
  L"moonsysh.dll",                // Moon Secure Antivirus.
  L"npdivx32.dll",                // DivX.
  L"npggNT.des",                  // GameGuard 2008.
  L"npggNT.dll",                  // GameGuard (older).
  L"oawatch.dll",                 // Online Armor.
  L"pavhook.dll",                 // Panda Internet Security.
  L"pavshook.dll",                // Panda Antivirus.
  L"pctavhook.dll",               // PC Tools Antivirus.
  L"pctgmhk.dll",                 // PC Tools Spyware Doctor.
  L"prntrack.dll",                // Pharos Systems.
  L"radhslib.dll",                // Radiant Naomi Internet Filter.
  L"radprlib.dll",                // Radiant Naomi Internet Filter.
  L"rlhook.dll",                  // Trustware Bufferzone.
  L"r3hook.dll",                  // Kaspersky Internet Security.
  L"sahook.dll",                  // McAfee Site Advisor.
  L"sbrige.dll",                  // Unknown.
  L"sc2hook.dll",                 // Supercopier 2.
  L"sguard.dll",                  // Iolo (System Guard).
  L"smum32.dll",                  // Spyware Doctor version 6.
  L"smumhook.dll",                // Spyware Doctor version 5.
  L"ssldivx.dll",                 // DivX.
  L"syncor11.dll",                // SynthCore Midi interface.
  L"systools.dll",                // Panda Antivirus.
  L"tfwah.dll",                   // Threatfire (PC tools).
  L"wblind.dll",                  // Stardock Object desktop.
  L"wbhelp.dll",                  // Stardock Object desktop.
  L"winstylerthemehelper.dll"     // Tuneup utilities 2006.
};

enum PluginPolicyCategory {
  PLUGIN_GROUP_TRUSTED,
  PLUGIN_GROUP_UNTRUSTED,
};

// Returns the policy category for the plugin dll.
PluginPolicyCategory GetPolicyCategoryForPlugin(
    const std::wstring& dll,
    const std::wstring& list) {
  std::wstring filename = FilePath(dll).BaseName().value();
  std::wstring plugin_dll = StringToLowerASCII(filename);
  std::wstring trusted_plugins = StringToLowerASCII(list);

  size_t pos = 0;
  size_t end_item = 0;
  while (end_item != std::wstring::npos) {
    end_item = list.find(L",", pos);

    size_t size_item = (end_item == std::wstring::npos) ? end_item :
                                                          end_item - pos;
    std::wstring item = list.substr(pos, size_item);
    if (!item.empty() && item == plugin_dll)
      return PLUGIN_GROUP_TRUSTED;

    pos = end_item + 1;
  }

  return PLUGIN_GROUP_UNTRUSTED;
}

// Adds the policy rules for the path and path\ with the semantic |access|.
// If |children| is set to true, we need to add the wildcard rules to also
// apply the rule to the subfiles and subfolders.
bool AddDirectory(int path, const wchar_t* sub_dir, bool children,
                  sandbox::TargetPolicy::Semantics access,
                  sandbox::TargetPolicy* policy) {
  std::wstring directory;
  if (!PathService::Get(path, &directory))
    return false;

  if (sub_dir) {
    file_util::AppendToPath(&directory, sub_dir);
    file_util::AbsolutePath(&directory);
  }

  sandbox::ResultCode result;
  result = policy->AddRule(sandbox::TargetPolicy::SUBSYS_FILES, access,
                           directory.c_str());
  if (result != sandbox::SBOX_ALL_OK)
    return false;

  if (children)
    file_util::AppendToPath(&directory, L"*");
  else
    // Add the version of the path that ends with a separator.
    file_util::AppendToPath(&directory, L"");

  result = policy->AddRule(sandbox::TargetPolicy::SUBSYS_FILES, access,
                           directory.c_str());
  if (result != sandbox::SBOX_ALL_OK)
    return false;

  return true;
}

// Adds the policy rules for the path and path\* with the semantic |access|.
// We need to add the wildcard rules to also apply the rule to the subkeys.
bool AddKeyAndSubkeys(std::wstring key,
                      sandbox::TargetPolicy::Semantics access,
                      sandbox::TargetPolicy* policy) {
  sandbox::ResultCode result;
  result = policy->AddRule(sandbox::TargetPolicy::SUBSYS_REGISTRY, access,
                           key.c_str());
  if (result != sandbox::SBOX_ALL_OK)
    return false;

  key += L"\\*";
  result = policy->AddRule(sandbox::TargetPolicy::SUBSYS_REGISTRY, access,
                           key.c_str());
  if (result != sandbox::SBOX_ALL_OK)
    return false;

  return true;
}

// Adds policy rules for unloaded the known dlls that cause chrome to crash.
// Eviction of injected DLLs is done by the sandbox so that the injected module
// does not get a chance to execute any code.
void AddDllEvictionPolicy(sandbox::TargetPolicy* policy) {
  for (int ix = 0; ix != arraysize(kTroublesomeDlls); ++ix) {
    // To minimize the list we only add an unload policy if the dll is also
    // loaded in this process. All the injected dlls of interest do this.
    if (::GetModuleHandleW(kTroublesomeDlls[ix])) {
      LOG(INFO) << "dll to unload found: " << kTroublesomeDlls[ix];
      policy->AddDllToUnload(kTroublesomeDlls[ix]);
    }
  }
}

// Adds the generic policy rules to a sandbox TargetPolicy.
bool AddGenericPolicy(sandbox::TargetPolicy* policy) {
  sandbox::ResultCode result;

  // Add the policy for the pipes
  result = policy->AddRule(sandbox::TargetPolicy::SUBSYS_FILES,
                           sandbox::TargetPolicy::FILES_ALLOW_ANY,
                           L"\\??\\pipe\\chrome.*");
  if (result != sandbox::SBOX_ALL_OK)
    return false;

  result = policy->AddRule(sandbox::TargetPolicy::SUBSYS_NAMED_PIPES,
                           sandbox::TargetPolicy::NAMEDPIPES_ALLOW_ANY,
                           L"\\\\.\\pipe\\chrome.nacl.*");
  if (result != sandbox::SBOX_ALL_OK)
    return false;

  // Add the policy for debug message only in debug
#ifndef NDEBUG
  std::wstring debug_message;
  if (!PathService::Get(chrome::DIR_APP, &debug_message))
    return false;
  if (!win_util::ConvertToLongPath(debug_message, &debug_message))
    return false;
  file_util::AppendToPath(&debug_message, L"debug_message.exe");
  result = policy->AddRule(sandbox::TargetPolicy::SUBSYS_PROCESS,
                           sandbox::TargetPolicy::PROCESS_MIN_EXEC,
                           debug_message.c_str());
  if (result != sandbox::SBOX_ALL_OK)
    return false;
#endif  // NDEBUG

  return true;
}

// Creates a sandbox without any restriction.
bool ApplyPolicyForTrustedPlugin(sandbox::TargetPolicy* policy) {
  policy->SetJobLevel(sandbox::JOB_UNPROTECTED, 0);
  policy->SetTokenLevel(sandbox::USER_UNPROTECTED, sandbox::USER_UNPROTECTED);
  return true;
}

// Creates a sandbox with the plugin running in a restricted environment.
// Only the "Users" and "Everyone" groups are enabled in the token. The User SID
// is disabled.
bool ApplyPolicyForUntrustedPlugin(sandbox::TargetPolicy* policy) {
  policy->SetJobLevel(sandbox::JOB_UNPROTECTED, 0);

  sandbox::TokenLevel initial_token = sandbox::USER_UNPROTECTED;
  if (win_util::GetWinVersion() > win_util::WINVERSION_XP) {
    // On 2003/Vista the initial token has to be restricted if the main token
    // is restricted.
    initial_token = sandbox::USER_RESTRICTED_SAME_ACCESS;
  }
  policy->SetTokenLevel(initial_token, sandbox::USER_LIMITED);
  policy->SetDelayedIntegrityLevel(sandbox::INTEGRITY_LEVEL_LOW);

  if (!AddDirectory(base::DIR_TEMP, NULL, true,
                    sandbox::TargetPolicy::FILES_ALLOW_ANY, policy))
    return false;

  if (!AddDirectory(base::DIR_IE_INTERNET_CACHE, NULL, true,
                    sandbox::TargetPolicy::FILES_ALLOW_ANY, policy))
    return false;

  if (!AddDirectory(base::DIR_APP_DATA, NULL, true,
                    sandbox::TargetPolicy::FILES_ALLOW_READONLY,
                    policy))
    return false;

  if (!AddDirectory(base::DIR_PROFILE, NULL, false,  /*not recursive*/
                    sandbox::TargetPolicy::FILES_ALLOW_READONLY,
                    policy))
    return false;

  if (!AddDirectory(base::DIR_APP_DATA, L"Adobe", true,
                    sandbox::TargetPolicy::FILES_ALLOW_ANY,
                    policy))
    return false;

  if (!AddDirectory(base::DIR_APP_DATA, L"Macromedia", true,
                    sandbox::TargetPolicy::FILES_ALLOW_ANY,
                    policy))
    return false;

  if (!AddDirectory(base::DIR_LOCAL_APP_DATA, NULL, true,
                    sandbox::TargetPolicy::FILES_ALLOW_READONLY,
                    policy))
    return false;

  if (!AddKeyAndSubkeys(L"HKEY_CURRENT_USER\\SOFTWARE\\ADOBE",
                        sandbox::TargetPolicy::REG_ALLOW_ANY,
                        policy))
    return false;

  if (!AddKeyAndSubkeys(L"HKEY_CURRENT_USER\\SOFTWARE\\MACROMEDIA",
                        sandbox::TargetPolicy::REG_ALLOW_ANY,
                        policy))
    return false;

  if (win_util::GetWinVersion() >= win_util::WINVERSION_VISTA) {
    if (!AddKeyAndSubkeys(L"HKEY_CURRENT_USER\\SOFTWARE\\AppDataLow",
                          sandbox::TargetPolicy::REG_ALLOW_ANY,
                          policy))
      return false;

    if (!AddDirectory(base::DIR_LOCAL_APP_DATA_LOW, NULL, true,
                      sandbox::TargetPolicy::FILES_ALLOW_ANY,
                      policy))
      return false;

    // DIR_APP_DATA is AppData\Roaming, but Adobe needs to do a directory
    // listing in AppData directly, so we add a non-recursive policy for
    // AppData itself.
    if (!AddDirectory(base::DIR_APP_DATA, L"..", false,
                      sandbox::TargetPolicy::FILES_ALLOW_READONLY,
                      policy))
      return false;
  }

  return true;
}

// Launches the privileged flash broker, used when flash is sandboxed.
// The broker is the same flash dll, except that it uses a different
// entrypoint (BrokerMain) and it is hosted in windows' generic surrogate
// process rundll32. After launching the broker we need to pass to
// the flash plugin the process id of the broker via the command line
// using --flash-broker=pid.
// More info about rundll32 at http://support.microsoft.com/kb/164787.
bool LoadFlashBroker(const FilePath& plugin_path, CommandLine* cmd_line) {
  FilePath rundll;
  if (!PathService::Get(base::DIR_SYSTEM, &rundll))
    return false;
  rundll = rundll.AppendASCII("rundll32.exe");
  // Rundll32 cannot handle paths with spaces, so we use the short path.
  wchar_t short_path[MAX_PATH];
  if (0 == ::GetShortPathNameW(plugin_path.value().c_str(),
                               short_path, arraysize(short_path)))
    return false;
  std::wstring cmd_final =
      base::StringPrintf(L"%ls %ls,BrokerMain browser=chrome",
                         rundll.value().c_str(),
                         short_path);
  base::ProcessHandle process;
  if (!base::LaunchApp(cmd_final, false, true, &process))
    return false;

  cmd_line->AppendSwitchASCII("flash-broker",
                              base::Int64ToString(::GetProcessId(process)));
  ::CloseHandle(process);
  return true;
}

// Creates a sandbox for the built-in flash plugin running in a restricted
// environment. This is a work in progress and for the time being do not
// pay attention to the duplication between this function and the above
// function. For more information see bug 50796.
bool ApplyPolicyForBuiltInFlashPlugin(sandbox::TargetPolicy* policy) {
  // TODO(cpu): Lock down the job level more.
  policy->SetJobLevel(sandbox::JOB_UNPROTECTED, 0);

  sandbox::TokenLevel initial_token = sandbox::USER_UNPROTECTED;

  if (win_util::GetWinVersion() > win_util::WINVERSION_XP)
    initial_token = sandbox::USER_RESTRICTED_SAME_ACCESS;

  policy->SetTokenLevel(initial_token, sandbox::USER_LIMITED);

  policy->SetDelayedIntegrityLevel(sandbox::INTEGRITY_LEVEL_LOW);

  // TODO(cpu): Proxy registry access and remove these policies.
  if (!AddKeyAndSubkeys(L"HKEY_CURRENT_USER\\SOFTWARE\\ADOBE",
                        sandbox::TargetPolicy::REG_ALLOW_ANY,
                        policy))
    return false;

  if (!AddKeyAndSubkeys(L"HKEY_CURRENT_USER\\SOFTWARE\\MACROMEDIA",
                        sandbox::TargetPolicy::REG_ALLOW_ANY,
                        policy))
    return false;
  return true;
}

// Adds the custom policy rules for a given plugin. |trusted_plugins| contains
// the comma separate list of plugin dll names that should not be sandboxed.
bool AddPolicyForPlugin(CommandLine* cmd_line,
                        sandbox::TargetPolicy* policy) {
  std::wstring plugin_dll = cmd_line->
      GetSwitchValueNative(switches::kPluginPath);
  std::wstring trusted_plugins = CommandLine::ForCurrentProcess()->
      GetSwitchValueNative(switches::kTrustedPlugins);
  // Add the policy for the pipes.
  sandbox::ResultCode result = sandbox::SBOX_ALL_OK;
  result = policy->AddRule(sandbox::TargetPolicy::SUBSYS_NAMED_PIPES,
                           sandbox::TargetPolicy::NAMEDPIPES_ALLOW_ANY,
                           L"\\\\.\\pipe\\chrome.*");
  if (result != sandbox::SBOX_ALL_OK) {
    NOTREACHED();
    return false;
  }

  // The built-in flash gets a custom, more restricted sandbox.
  FilePath builtin_flash;
  if (PathService::Get(chrome::FILE_FLASH_PLUGIN, &builtin_flash)) {
    FilePath plugin_path(plugin_dll);
    if (plugin_path == builtin_flash) {
      // Spawn the flash broker and apply sandbox policy.
      if (!LoadFlashBroker(plugin_path, cmd_line)) {
        // Could not start the broker, use a very weak policy instead.
        DLOG(WARNING) << "Failed to start flash broker";
        return ApplyPolicyForTrustedPlugin(policy);
      }
      return ApplyPolicyForBuiltInFlashPlugin(policy);
    }
  }

  PluginPolicyCategory policy_category =
      GetPolicyCategoryForPlugin(plugin_dll, trusted_plugins);

  switch (policy_category) {
    case PLUGIN_GROUP_TRUSTED:
      return ApplyPolicyForTrustedPlugin(policy);
    case PLUGIN_GROUP_UNTRUSTED:
      return ApplyPolicyForUntrustedPlugin(policy);
    default:
      NOTREACHED();
      break;
  }

  return false;
}

void AddPolicyForRenderer(sandbox::TargetPolicy* policy,
                          bool* on_sandbox_desktop) {
  policy->SetJobLevel(sandbox::JOB_LOCKDOWN, 0);

  sandbox::TokenLevel initial_token = sandbox::USER_UNPROTECTED;
  if (win_util::GetWinVersion() > win_util::WINVERSION_XP) {
    // On 2003/Vista the initial token has to be restricted if the main
    // token is restricted.
    initial_token = sandbox::USER_RESTRICTED_SAME_ACCESS;
  }

  policy->SetTokenLevel(initial_token, sandbox::USER_LOCKDOWN);
  policy->SetDelayedIntegrityLevel(sandbox::INTEGRITY_LEVEL_LOW);

  bool use_winsta = !CommandLine::ForCurrentProcess()->HasSwitch(
                        switches::kDisableAltWinstation);

  if (sandbox::SBOX_ALL_OK ==  policy->SetAlternateDesktop(use_winsta)) {
    *on_sandbox_desktop = true;
  } else {
    *on_sandbox_desktop = false;
    DLOG(WARNING) << "Failed to apply desktop security to the renderer";
  }

  AddDllEvictionPolicy(policy);
}

}  // namespace

namespace sandbox {

void InitBrokerServices(sandbox::BrokerServices* broker_services) {
  // TODO(abarth): DCHECK(CalledOnValidThread());
  //               See <http://b/1287166>.
  CHECK(broker_services);
  CHECK(!g_broker_services);
  broker_services->Init();
  g_broker_services = broker_services;
}

base::ProcessHandle StartProcessWithAccess(CommandLine* cmd_line,
                                           const FilePath& exposed_dir) {
  base::ProcessHandle process = 0;
  const CommandLine& browser_command_line = *CommandLine::ForCurrentProcess();
  ChildProcessInfo::ProcessType type;
  std::string type_str = cmd_line->GetSwitchValueASCII(switches::kProcessType);
  if (type_str == switches::kRendererProcess) {
    type = ChildProcessInfo::RENDER_PROCESS;
  } else if (type_str == switches::kExtensionProcess) {
    // Extensions are just renderers with another name.
    type = ChildProcessInfo::RENDER_PROCESS;
  } else if (type_str == switches::kPluginProcess) {
    type = ChildProcessInfo::PLUGIN_PROCESS;
  } else if (type_str == switches::kWorkerProcess) {
    type = ChildProcessInfo::WORKER_PROCESS;
  } else if (type_str == switches::kNaClLoaderProcess) {
    type = ChildProcessInfo::NACL_LOADER_PROCESS;
  } else if (type_str == switches::kUtilityProcess) {
    type = ChildProcessInfo::UTILITY_PROCESS;
  } else if (type_str == switches::kNaClBrokerProcess) {
    type = ChildProcessInfo::NACL_BROKER_PROCESS;
  } else if (type_str == switches::kGpuProcess) {
    type = ChildProcessInfo::GPU_PROCESS;
  } else {
    NOTREACHED();
    return 0;
  }

  TRACE_EVENT_BEGIN("StartProcessWithAccess", 0, type_str);

  bool in_sandbox =
      (type != ChildProcessInfo::NACL_BROKER_PROCESS) &&
      !browser_command_line.HasSwitch(switches::kNoSandbox) &&
      (type != ChildProcessInfo::PLUGIN_PROCESS ||
       browser_command_line.HasSwitch(switches::kSafePlugins)) &&
      (type != ChildProcessInfo::GPU_PROCESS);
#if !defined (GOOGLE_CHROME_BUILD)
  if (browser_command_line.HasSwitch(switches::kInProcessPlugins)) {
    // In process plugins won't work if the sandbox is enabled.
    in_sandbox = false;
  }
#endif
  if (!browser_command_line.HasSwitch(switches::kDisableExperimentalWebGL) &&
      browser_command_line.HasSwitch(switches::kInProcessWebGL)) {
    // In process WebGL won't work if the sandbox is enabled.
    in_sandbox = false;
  }

  // Propagate the Chrome Frame flag to sandboxed processes if present.
  if (browser_command_line.HasSwitch(switches::kChromeFrame)) {
    if (!cmd_line->HasSwitch(switches::kChromeFrame)) {
      cmd_line->AppendSwitch(switches::kChromeFrame);
    }
  }

  bool child_needs_help =
      DebugFlags::ProcessDebugFlags(cmd_line, type, in_sandbox);

  // Prefetch hints on windows:
  // Using a different prefetch profile per process type will allow Windows
  // to create separate pretetch settings for browser, renderer etc.
  cmd_line->AppendArg(StringPrintf("/prefetch:%d", type));

  if (!in_sandbox) {
    base::LaunchApp(*cmd_line, false, false, &process);
    return process;
  }

  sandbox::ResultCode result;
  PROCESS_INFORMATION target = {0};
  sandbox::TargetPolicy* policy = g_broker_services->CreatePolicy();

  bool on_sandbox_desktop = false;
  if (type == ChildProcessInfo::PLUGIN_PROCESS) {
    if (!AddPolicyForPlugin(cmd_line, policy))
      return 0;
  } else {
    AddPolicyForRenderer(policy, &on_sandbox_desktop);

    if (type_str != switches::kRendererProcess) {
      // Hack for Google Desktop crash. Trick GD into not injecting its DLL into
      // this subprocess. See
      // http://code.google.com/p/chromium/issues/detail?id=25580
      cmd_line->AppendSwitchASCII("ignored", " --type=renderer ");
    }
  }

  if (!exposed_dir.empty()) {
    result = policy->AddRule(sandbox::TargetPolicy::SUBSYS_FILES,
                             sandbox::TargetPolicy::FILES_ALLOW_ANY,
                             exposed_dir.ToWStringHack().c_str());
    if (result != sandbox::SBOX_ALL_OK)
      return 0;

    FilePath exposed_files = exposed_dir.AppendASCII("*");
    result = policy->AddRule(sandbox::TargetPolicy::SUBSYS_FILES,
                             sandbox::TargetPolicy::FILES_ALLOW_ANY,
                             exposed_files.ToWStringHack().c_str());
    if (result != sandbox::SBOX_ALL_OK)
      return 0;
  }

  if (!AddGenericPolicy(policy)) {
    NOTREACHED();
    return 0;
  }

  TRACE_EVENT_BEGIN("StartProcessWithAccess::LAUNCHPROCESS", 0, 0);

  result = g_broker_services->SpawnTarget(
      cmd_line->program().c_str(),
      cmd_line->command_line_string().c_str(),
      policy, &target);
  policy->Release();

  TRACE_EVENT_END("StartProcessWithAccess::LAUNCHPROCESS", 0, 0);

  if (sandbox::SBOX_ALL_OK != result)
    return 0;

  ResumeThread(target.hThread);
  CloseHandle(target.hThread);
  process = target.hProcess;

  // Help the process a little. It can't start the debugger by itself if
  // the process is in a sandbox.
  if (child_needs_help)
    DebugUtil::SpawnDebuggerOnProcess(target.dwProcessId);

  return process;
}

}  // namespace sandbox
