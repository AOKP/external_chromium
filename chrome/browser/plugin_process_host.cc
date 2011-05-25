// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/plugin_process_host.h"

#if defined(OS_WIN)
#include <windows.h>
#elif defined(OS_POSIX)
#include <utility>  // for pair<>
#endif

#include <vector>

#include "app/app_switches.h"
#include "base/command_line.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/browser_thread.h"
#include "chrome/browser/child_process_security_policy.h"
#include "chrome/browser/chrome_plugin_browsing_context.h"
#include "chrome/browser/net/url_request_tracking.h"
#include "chrome/browser/plugin_download_helper.h"
#include "chrome/browser/plugin_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/renderer_host/resource_dispatcher_host.h"
#include "chrome/browser/renderer_host/resource_message_filter.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_plugin_lib.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/logging_chrome.h"
#include "chrome/common/net/url_request_context_getter.h"
#include "chrome/common/plugin_messages.h"
#include "chrome/common/render_messages.h"
#include "gfx/native_widget_types.h"
#include "ipc/ipc_switches.h"
#include "net/base/cookie_store.h"
#include "net/base/io_buffer.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context.h"

#if defined(USE_X11)
#include "gfx/gtk_native_view_id_manager.h"
#endif

#if defined(OS_MACOSX)
#include "base/mac/mac_util.h"
#include "chrome/common/plugin_carbon_interpose_constants_mac.h"
#include "gfx/rect.h"
#endif

static const char kDefaultPluginFinderURL[] =
    "https://dl-ssl.google.com/edgedl/chrome/plugins/plugins2.xml";

namespace {

// Helper class that we pass to ResourceMessageFilter so that it can find the
// right URLRequestContext for a request.
class PluginURLRequestContextOverride
    : public ResourceMessageFilter::URLRequestContextOverride {
 public:
  PluginURLRequestContextOverride() {
  }

  virtual URLRequestContext* GetRequestContext(
      uint32 request_id, ResourceType::Type resource_type) {
    return CPBrowsingContextManager::GetInstance()->ToURLRequestContext(
        request_id);
  }

 private:
  virtual ~PluginURLRequestContextOverride() {}
};

}  // namespace

#if defined(OS_WIN)
void PluginProcessHost::OnPluginWindowDestroyed(HWND window, HWND parent) {
  // The window is destroyed at this point, we just care about its parent, which
  // is the intermediate window we created.
  std::set<HWND>::iterator window_index =
      plugin_parent_windows_set_.find(parent);
  if (window_index == plugin_parent_windows_set_.end())
    return;

  plugin_parent_windows_set_.erase(window_index);
  PostMessage(parent, WM_CLOSE, 0, 0);
}

void PluginProcessHost::OnDownloadUrl(const std::string& url,
                                      int source_pid,
                                      gfx::NativeWindow caller_window) {
  PluginDownloadUrlHelper* download_url_helper =
      new PluginDownloadUrlHelper(url, source_pid, caller_window, NULL);
  download_url_helper->InitiateDownload(
      Profile::GetDefaultRequestContext()->GetURLRequestContext());
}

void PluginProcessHost::AddWindow(HWND window) {
  plugin_parent_windows_set_.insert(window);
}

#endif  // defined(OS_WIN)

#if defined(TOOLKIT_USES_GTK)
void PluginProcessHost::OnMapNativeViewId(gfx::NativeViewId id,
                                          gfx::PluginWindowHandle* output) {
  *output = 0;
  GtkNativeViewManager::GetInstance()->GetXIDForId(output, id);
}
#endif  // defined(TOOLKIT_USES_GTK)

PluginProcessHost::PluginProcessHost()
    : BrowserChildProcessHost(
          PLUGIN_PROCESS,
          PluginService::GetInstance()->resource_dispatcher_host(),
          new PluginURLRequestContextOverride()),
      ALLOW_THIS_IN_INITIALIZER_LIST(resolve_proxy_msg_helper_(this, NULL))
#if defined(OS_MACOSX)
      , plugin_cursor_visible_(true)
#endif
{
}

PluginProcessHost::~PluginProcessHost() {
#if defined(OS_WIN)
  // We erase HWNDs from the plugin_parent_windows_set_ when we receive a
  // notification that the window is being destroyed. If we don't receive this
  // notification and the PluginProcessHost instance is being destroyed, it
  // means that the plugin process crashed. We paint a sad face in this case in
  // the renderer process. To ensure that the sad face shows up, and we don't
  // leak HWNDs, we should destroy existing plugin parent windows.
  std::set<HWND>::iterator window_index;
  for (window_index = plugin_parent_windows_set_.begin();
       window_index != plugin_parent_windows_set_.end();
       window_index++) {
    PostMessage(*window_index, WM_CLOSE, 0, 0);
  }
#elif defined(OS_MACOSX)
  // If the plugin process crashed but had fullscreen windows open at the time,
  // make sure that the menu bar is visible.
  std::set<uint32>::iterator window_index;
  for (window_index = plugin_fullscreen_windows_set_.begin();
       window_index != plugin_fullscreen_windows_set_.end();
       window_index++) {
    if (BrowserThread::CurrentlyOn(BrowserThread::UI)) {
      base::mac::ReleaseFullScreen(base::mac::kFullScreenModeHideAll);
    } else {
      BrowserThread::PostTask(
          BrowserThread::UI, FROM_HERE,
          NewRunnableFunction(base::mac::ReleaseFullScreen,
                              base::mac::kFullScreenModeHideAll));
    }
  }
  // If the plugin hid the cursor, reset that.
  if (!plugin_cursor_visible_) {
    if (BrowserThread::CurrentlyOn(BrowserThread::UI)) {
      base::mac::SetCursorVisibility(true);
    } else {
      BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        NewRunnableFunction(base::mac::SetCursorVisibility,
                            true));
    }
  }
#endif
  // Cancel all pending and sent requests.
  CancelRequests();
}

bool PluginProcessHost::Init(const webkit::npapi::WebPluginInfo& info,
                             const std::string& locale) {
  info_ = info;
  set_name(UTF16ToWideHack(info_.name));
  set_version(UTF16ToWideHack(info_.version));

  if (!CreateChannel())
    return false;

  // Build command line for plugin. When we have a plugin launcher, we can't
  // allow "self" on linux and we need the real file path.
  const CommandLine& browser_command_line = *CommandLine::ForCurrentProcess();
  CommandLine::StringType plugin_launcher =
      browser_command_line.GetSwitchValueNative(switches::kPluginLauncher);
  FilePath exe_path = GetChildPath(plugin_launcher.empty());
  if (exe_path.empty())
    return false;

  CommandLine* cmd_line = new CommandLine(exe_path);
  // Put the process type and plugin path first so they're easier to see
  // in process listings using native process management tools.
  cmd_line->AppendSwitchASCII(switches::kProcessType, switches::kPluginProcess);
  cmd_line->AppendSwitchPath(switches::kPluginPath, info.path);

  if (logging::DialogsAreSuppressed())
    cmd_line->AppendSwitch(switches::kNoErrorDialogs);

  // Propagate the following switches to the plugin command line (along with
  // any associated values) if present in the browser command line
  static const char* const kSwitchNames[] = {
    switches::kPluginStartupDialog,
    switches::kNoSandbox,
    switches::kSafePlugins,
    switches::kTestSandbox,
    switches::kUserAgent,
    switches::kDisableBreakpad,
    switches::kFullMemoryCrashReport,
    switches::kEnableLogging,
    switches::kDisableLogging,
    switches::kLoggingLevel,
    switches::kLogPluginMessages,
    switches::kUserDataDir,
    switches::kEnableDCHECK,
    switches::kSilentDumpOnDCHECK,
    switches::kMemoryProfiling,
    switches::kUseLowFragHeapCrt,
    switches::kEnableStatsTable,
    switches::kEnableGPUPlugin,
    switches::kUseGL,
#if defined(OS_CHROMEOS)
    switches::kLoginProfile,
#endif
  };

  cmd_line->CopySwitchesFrom(browser_command_line, kSwitchNames,
                             arraysize(kSwitchNames));

  // If specified, prepend a launcher program to the command line.
  if (!plugin_launcher.empty())
    cmd_line->PrependWrapper(plugin_launcher);

  if (!locale.empty()) {
    // Pass on the locale so the null plugin will use the right language in the
    // prompt to install the desired plugin.
    cmd_line->AppendSwitchASCII(switches::kLang, locale);
  }

  // Gears requires the data dir to be available on startup.
  FilePath data_dir =
    PluginService::GetInstance()->GetChromePluginDataDir();
  DCHECK(!data_dir.empty());
  cmd_line->AppendSwitchPath(switches::kPluginDataDir, data_dir);

  cmd_line->AppendSwitchASCII(switches::kProcessChannelID, channel_id());

  SetCrashReporterCommandLine(cmd_line);

#if defined(OS_POSIX)
  base::environment_vector env;
#if defined(OS_MACOSX) && !defined(__LP64__)
  // Add our interposing library for Carbon. This is stripped back out in
  // plugin_main.cc, so changes here should be reflected there.
  std::string interpose_list(plugin_interpose_strings::kInterposeLibraryPath);
  const char* existing_list =
      getenv(plugin_interpose_strings::kDYLDInsertLibrariesKey);
  if (existing_list) {
    interpose_list.insert(0, ":");
    interpose_list.insert(0, existing_list);
  }
  env.push_back(std::pair<std::string, std::string>(
      plugin_interpose_strings::kDYLDInsertLibrariesKey,
      interpose_list));
#endif
#endif

  Launch(
#if defined(OS_WIN)
      FilePath(),
#elif defined(OS_POSIX)
      false,
      env,
#endif
      cmd_line);

  return true;
}

void PluginProcessHost::ForceShutdown() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  Send(new PluginProcessMsg_NotifyRenderersOfPendingShutdown());
  BrowserChildProcessHost::ForceShutdown();
}

void PluginProcessHost::OnProcessLaunched() {
  FilePath gears_path;
  if (PathService::Get(chrome::FILE_GEARS_PLUGIN, &gears_path)) {
    FilePath::StringType gears_path_lc = StringToLowerASCII(gears_path.value());
    FilePath::StringType plugin_path_lc =
        StringToLowerASCII(info_.path.value());
    if (plugin_path_lc == gears_path_lc) {
      // Give Gears plugins "background" priority.  See http://b/1280317.
      SetProcessBackgrounded();
    }
  }
}

bool PluginProcessHost::OnMessageReceived(const IPC::Message& msg) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(PluginProcessHost, msg)
    IPC_MESSAGE_HANDLER(PluginProcessHostMsg_ChannelCreated, OnChannelCreated)
    IPC_MESSAGE_HANDLER(PluginProcessHostMsg_GetPluginFinderUrl,
                        OnGetPluginFinderUrl)
    IPC_MESSAGE_HANDLER(PluginProcessHostMsg_PluginMessage, OnPluginMessage)
    IPC_MESSAGE_HANDLER(PluginProcessHostMsg_GetCookies, OnGetCookies)
    IPC_MESSAGE_HANDLER(PluginProcessHostMsg_AccessFiles, OnAccessFiles)
    IPC_MESSAGE_HANDLER_DELAY_REPLY(PluginProcessHostMsg_ResolveProxy,
                                    OnResolveProxy)
#if defined(OS_WIN)
    IPC_MESSAGE_HANDLER(PluginProcessHostMsg_PluginWindowDestroyed,
                        OnPluginWindowDestroyed)
    IPC_MESSAGE_HANDLER(PluginProcessHostMsg_DownloadUrl, OnDownloadUrl)
#endif
#if defined(TOOLKIT_USES_GTK)
    IPC_MESSAGE_HANDLER(PluginProcessHostMsg_MapNativeViewId,
                        OnMapNativeViewId)
#endif
#if defined(OS_MACOSX)
    IPC_MESSAGE_HANDLER(PluginProcessHostMsg_PluginSelectWindow,
                        OnPluginSelectWindow)
    IPC_MESSAGE_HANDLER(PluginProcessHostMsg_PluginShowWindow,
                        OnPluginShowWindow)
    IPC_MESSAGE_HANDLER(PluginProcessHostMsg_PluginHideWindow,
                        OnPluginHideWindow)
    IPC_MESSAGE_HANDLER(PluginProcessHostMsg_PluginSetCursorVisibility,
                        OnPluginSetCursorVisibility)
#endif
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()

  DCHECK(handled);
  return handled;
}

void PluginProcessHost::OnChannelConnected(int32 peer_pid) {
  for (size_t i = 0; i < pending_requests_.size(); ++i) {
    RequestPluginChannel(pending_requests_[i]);
  }

  pending_requests_.clear();
}

void PluginProcessHost::OnChannelError() {
  CancelRequests();
}

bool PluginProcessHost::CanShutdown() {
  return sent_requests_.empty();
}

void PluginProcessHost::CancelRequests() {
  for (size_t i = 0; i < pending_requests_.size(); ++i)
    pending_requests_[i]->OnError();
  pending_requests_.clear();

  while (!sent_requests_.empty()) {
    sent_requests_.front()->OnError();
    sent_requests_.pop();
  }
}

void PluginProcessHost::OpenChannelToPlugin(Client* client) {
  InstanceCreated();
  client->SetPluginInfo(info_);
  if (opening_channel()) {
    // The channel is already in the process of being opened.  Put
    // this "open channel" request into a queue of requests that will
    // be run once the channel is open.
    pending_requests_.push_back(client);
    return;
  }

  // We already have an open channel, send a request right away to plugin.
  RequestPluginChannel(client);
}

void PluginProcessHost::OnGetCookies(uint32 request_context,
                                     const GURL& url,
                                     std::string* cookies) {
  URLRequestContext* context = CPBrowsingContextManager::GetInstance()->
        ToURLRequestContext(request_context);
  // TODO(mpcomplete): remove fallback case when Gears support is prevalent.
  if (!context)
    context = Profile::GetDefaultRequestContext()->GetURLRequestContext();

  // Note: We don't have a first_party_for_cookies check because plugins bypass
  // third-party cookie blocking.
  if (context && context->cookie_store()) {
    *cookies = context->cookie_store()->GetCookies(url);
  } else {
    DLOG(ERROR) << "Could not serve plugin cookies request.";
    cookies->clear();
  }
}

void PluginProcessHost::OnAccessFiles(int renderer_id,
                                      const std::vector<std::string>& files,
                                      bool* allowed) {
  ChildProcessSecurityPolicy* policy =
      ChildProcessSecurityPolicy::GetInstance();

  for (size_t i = 0; i < files.size(); ++i) {
    const FilePath path = FilePath::FromWStringHack(UTF8ToWide(files[i]));
    if (!policy->CanReadFile(renderer_id, path)) {
      VLOG(1) << "Denied unauthorized request for file " << files[i];
      *allowed = false;
      return;
    }
  }

  *allowed = true;
}

void PluginProcessHost::OnResolveProxy(const GURL& url,
                                       IPC::Message* reply_msg) {
  resolve_proxy_msg_helper_.Start(url, reply_msg);
}

void PluginProcessHost::OnResolveProxyCompleted(IPC::Message* reply_msg,
                                                int result,
                                                const std::string& proxy_list) {
  PluginProcessHostMsg_ResolveProxy::WriteReplyParams(
      reply_msg, result, proxy_list);
  Send(reply_msg);
}

void PluginProcessHost::RequestPluginChannel(Client* client) {
  // We can't send any sync messages from the browser because it might lead to
  // a hang.  However this async messages must be answered right away by the
  // plugin process (i.e. unblocks a Send() call like a sync message) otherwise
  // a deadlock can occur if the plugin creation request from the renderer is
  // a result of a sync message by the plugin process.
  PluginProcessMsg_CreateChannel* msg =
      new PluginProcessMsg_CreateChannel(client->ID(),
                                         client->OffTheRecord());
  msg->set_unblock(true);
  if (Send(msg)) {
    sent_requests_.push(client);
  } else {
    client->OnError();
  }
}

void PluginProcessHost::OnChannelCreated(
    const IPC::ChannelHandle& channel_handle) {
  Client* client = sent_requests_.front();

  client->OnChannelOpened(channel_handle);
  sent_requests_.pop();
}

void PluginProcessHost::OnGetPluginFinderUrl(std::string* plugin_finder_url) {
  if (!plugin_finder_url) {
    NOTREACHED();
    return;
  }

  // TODO(iyengar)  Add the plumbing to retrieve the default
  // plugin finder URL.
  *plugin_finder_url = kDefaultPluginFinderURL;
}

void PluginProcessHost::OnPluginMessage(
    const std::vector<uint8>& data) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  ChromePluginLib *chrome_plugin = ChromePluginLib::Find(info_.path);
  if (chrome_plugin) {
    void *data_ptr = const_cast<void*>(reinterpret_cast<const void*>(&data[0]));
    uint32 data_len = static_cast<uint32>(data.size());
    chrome_plugin->functions().on_message(data_ptr, data_len);
  }
}
