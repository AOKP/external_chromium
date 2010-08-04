// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/glue/plugins/webplugin_delegate_impl.h"

#include <string>
#include <vector>

#include "base/file_util.h"
#include "base/message_loop.h"
#include "base/process_util.h"
#include "base/scoped_ptr.h"
#include "base/stats_counters.h"
#include "base/string_util.h"
#include "third_party/WebKit/WebKit/chromium/public/WebInputEvent.h"
#include "webkit/glue/plugins/plugin_constants_win.h"
#include "webkit/glue/plugins/plugin_instance.h"
#include "webkit/glue/plugins/plugin_lib.h"
#include "webkit/glue/plugins/plugin_list.h"
#include "webkit/glue/plugins/plugin_stream_url.h"
#include "webkit/glue/webkit_glue.h"

using webkit_glue::WebPlugin;
using webkit_glue::WebPluginDelegate;
using webkit_glue::WebPluginResourceClient;
using WebKit::WebCursorInfo;
using WebKit::WebKeyboardEvent;
using WebKit::WebInputEvent;
using WebKit::WebMouseEvent;

WebPluginDelegateImpl* WebPluginDelegateImpl::Create(
    const FilePath& filename,
    const std::string& mime_type,
    gfx::PluginWindowHandle containing_view) {
  scoped_refptr<NPAPI::PluginLib> plugin_lib =
      NPAPI::PluginLib::CreatePluginLib(filename);
  if (plugin_lib.get() == NULL)
    return NULL;

  NPError err = plugin_lib->NP_Initialize();
  if (err != NPERR_NO_ERROR)
    return NULL;

  scoped_refptr<NPAPI::PluginInstance> instance =
      plugin_lib->CreateInstance(mime_type);
  return new WebPluginDelegateImpl(containing_view, instance.get());
}

void WebPluginDelegateImpl::PluginDestroyed() {
  if (handle_event_depth_) {
    MessageLoop::current()->DeleteSoon(FROM_HERE, this);
  } else {
    delete this;
  }
}

bool WebPluginDelegateImpl::Initialize(
    const GURL& url,
    const std::vector<std::string>& arg_names,
    const std::vector<std::string>& arg_values,
    WebPlugin* plugin,
    bool load_manually) {
  plugin_ = plugin;

  instance_->set_web_plugin(plugin_);
  if (quirks_ & PLUGIN_QUIRK_DONT_ALLOW_MULTIPLE_INSTANCES) {
    NPAPI::PluginLib* plugin_lib = instance()->plugin_lib();
    if (plugin_lib->instance_count() > 1) {
      return false;
    }
  }

  if (quirks_ & PLUGIN_QUIRK_DIE_AFTER_UNLOAD)
    webkit_glue::SetForcefullyTerminatePluginProcess(true);

  int argc = 0;
  scoped_array<char*> argn(new char*[arg_names.size()]);
  scoped_array<char*> argv(new char*[arg_names.size()]);
  for (size_t i = 0; i < arg_names.size(); ++i) {
    if (quirks_ & PLUGIN_QUIRK_NO_WINDOWLESS &&
        LowerCaseEqualsASCII(arg_names[i], "windowlessvideo")) {
      continue;
    }
    argn[argc] = const_cast<char*>(arg_names[i].c_str());
    argv[argc] = const_cast<char*>(arg_values[i].c_str());
    argc++;
  }

  bool start_result = instance_->Start(
      url, argn.get(), argv.get(), argc, load_manually);
  if (!start_result)
    return false;

  windowless_ = instance_->windowless();
  if (!windowless_) {
    if (!WindowedCreatePlugin())
      return false;
  } else {
    // For windowless plugins we should set the containing window handle
    // as the instance window handle. This is what Safari does. Not having
    // a valid window handle causes subtle bugs with plugins which retrieve
    // the window handle and validate the same. The window handle can be
    // retrieved via NPN_GetValue of NPNVnetscapeWindow.
    instance_->set_window_handle(parent_);
  }

  bool should_load = PlatformInitialize();

  plugin_url_ = url.spec();

  return should_load;
}

void WebPluginDelegateImpl::DestroyInstance() {
  if (instance_ && (instance_->npp()->ndata != NULL)) {
    // Shutdown all streams before destroying so that
    // no streams are left "in progress".  Need to do
    // this before calling set_web_plugin(NULL) because the
    // instance uses the helper to do the download.
    instance_->CloseStreams();

    window_.window = NULL;
    if (!(quirks_ & PLUGIN_QUIRK_DONT_SET_NULL_WINDOW_HANDLE_ON_DESTROY)) {
      instance_->NPP_SetWindow(&window_);
    }

    instance_->NPP_Destroy();

    instance_->set_web_plugin(NULL);

    PlatformDestroyInstance();

    instance_ = 0;
  }
}

void WebPluginDelegateImpl::UpdateGeometry(
    const gfx::Rect& window_rect,
    const gfx::Rect& clip_rect) {

  if (first_set_window_call_) {
    first_set_window_call_ = false;
    // Plugins like media player on Windows have a bug where in they handle the
    // first geometry update and ignore the rest resulting in painting issues.
    // This quirk basically ignores the first set window call sequence for
    // these plugins and has been tested for Windows plugins only.
    if (quirks_ & PLUGIN_QUIRK_IGNORE_FIRST_SETWINDOW_CALL)
      return;
  }

  if (windowless_) {
    WindowlessUpdateGeometry(window_rect, clip_rect);
  } else {
    WindowedUpdateGeometry(window_rect, clip_rect);
  }
}

NPObject* WebPluginDelegateImpl::GetPluginScriptableObject() {
  return instance_->GetPluginScriptableObject();
}

void WebPluginDelegateImpl::DidFinishLoadWithReason(const GURL& url,
                                                    NPReason reason,
                                                    int notify_id) {
  if (quirks_ & PLUGIN_QUIRK_ALWAYS_NOTIFY_SUCCESS &&
      reason == NPRES_NETWORK_ERR) {
    // Flash needs this or otherwise it unloads the launching swf object.
    reason = NPRES_DONE;
  }

  instance()->DidFinishLoadWithReason(url, reason, notify_id);
}

int WebPluginDelegateImpl::GetProcessId() {
  // We are in process, so the plugin pid is this current process pid.
  return base::GetCurrentProcId();
}

void WebPluginDelegateImpl::SendJavaScriptStream(const GURL& url,
                                                 const std::string& result,
                                                 bool success,
                                                 int notify_id) {
  instance()->SendJavaScriptStream(url, result, success, notify_id);
}

void WebPluginDelegateImpl::DidReceiveManualResponse(
    const GURL& url, const std::string& mime_type,
    const std::string& headers, uint32 expected_length, uint32 last_modified) {
  if (!windowless_) {
    // Calling NPP_WriteReady before NPP_SetWindow causes movies to not load in
    // Flash.  See http://b/issue?id=892174.
    DCHECK(windowed_did_set_window_);
  }

  instance()->DidReceiveManualResponse(url, mime_type, headers,
                                       expected_length, last_modified);
}

void WebPluginDelegateImpl::DidReceiveManualData(const char* buffer,
                                                 int length) {
  instance()->DidReceiveManualData(buffer, length);
}

void WebPluginDelegateImpl::DidFinishManualLoading() {
  instance()->DidFinishManualLoading();
}

void WebPluginDelegateImpl::DidManualLoadFail() {
  instance()->DidManualLoadFail();
}

FilePath WebPluginDelegateImpl::GetPluginPath() {
  return instance()->plugin_lib()->plugin_info().path;
}

void WebPluginDelegateImpl::WindowedUpdateGeometry(
    const gfx::Rect& window_rect,
    const gfx::Rect& clip_rect) {
  if (WindowedReposition(window_rect, clip_rect) ||
      !windowed_did_set_window_) {
    // Let the plugin know that it has been moved
    WindowedSetWindow();
  }
}

bool WebPluginDelegateImpl::HandleInputEvent(const WebInputEvent& event,
                                             WebCursorInfo* cursor_info) {
  DCHECK(windowless_) << "events should only be received in windowless mode";

  bool pop_user_gesture = false;
  if (IsUserGesture(event)) {
    pop_user_gesture = true;
    instance()->PushPopupsEnabledState(true);
  }

  bool handled = PlatformHandleInputEvent(event, cursor_info);

  if (pop_user_gesture) {
    instance()->PopPopupsEnabledState();
  }

  return handled;
}

bool WebPluginDelegateImpl::IsUserGesture(const WebInputEvent& event) {
  switch (event.type) {
    case WebInputEvent::MouseDown:
    case WebInputEvent::MouseUp:
    case WebInputEvent::KeyDown:
    case WebInputEvent::KeyUp:
      return true;
    default:
      return false;
  }
  return false;
}

WebPluginResourceClient* WebPluginDelegateImpl::CreateResourceClient(
    unsigned long resource_id, const GURL& url, int notify_id) {
  return instance()->CreateStream(
      resource_id, url, std::string(), notify_id);
}

WebPluginResourceClient* WebPluginDelegateImpl::CreateSeekableResourceClient(
    unsigned long resource_id, int range_request_id) {
  return instance()->GetRangeRequest(range_request_id);
}
