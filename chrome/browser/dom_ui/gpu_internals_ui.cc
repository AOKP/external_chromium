// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/dom_ui/gpu_internals_ui.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "app/l10n_util.h"
#include "app/resource_bundle.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/message_loop.h"
#include "base/path_service.h"
#include "base/singleton.h"
#include "base/string_number_conversions.h"
#include "base/string_piece.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/browser_thread.h"
#include "chrome/browser/dom_ui/chrome_url_data_manager.h"
#include "chrome/browser/gpu_process_host.h"
#include "chrome/browser/gpu_process_host_ui_shim.h"
#include "chrome/browser/io_thread.h"
#include "chrome/browser/net/chrome_net_log.h"
#include "chrome/browser/net/connection_tester.h"
#include "chrome/browser/net/passive_log_collector.h"
#include "chrome/browser/net/url_fixer_upper.h"
#include "chrome/browser/platform_util.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_version_info.h"
#include "chrome/common/jstemplate_builder.h"
#include "chrome/common/net/url_request_context_getter.h"
#include "chrome/common/url_constants.h"
#include "grit/browser_resources.h"
#include "grit/generated_resources.h"
#include "net/base/escape.h"


namespace {

class GpuHTMLSource : public ChromeURLDataManager::DataSource {
 public:
  GpuHTMLSource();

  // Called when the network layer has requested a resource underneath
  // the path we registered.
  virtual void StartDataRequest(const std::string& path,
                                bool is_off_the_record,
                                int request_id);
  virtual std::string GetMimeType(const std::string&) const;

 private:
  ~GpuHTMLSource() {}
  DISALLOW_COPY_AND_ASSIGN(GpuHTMLSource);
};

// This class receives javascript messages from the renderer.
// Note that the DOMUI infrastructure runs on the UI thread, therefore all of
// this class's methods are expected to run on the UI thread.
class GpuMessageHandler
    : public DOMMessageHandler,
      public base::SupportsWeakPtr<GpuMessageHandler> {
 public:
  GpuMessageHandler();
  virtual ~GpuMessageHandler();

  // DOMMessageHandler implementation.
  virtual DOMMessageHandler* Attach(DOMUI* dom_ui);
  virtual void RegisterMessages();

  // Mesages
  void OnCallAsync(const ListValue* list);

  // Submessages dispatched from OnCallAsync
  Value* OnRequestGpuInfo(const ListValue* list);
  Value* OnRequestClientInfo(const ListValue* list);

  // Executes the javascript function |function_name| in the renderer, passing
  // it the argument |value|.
  void CallJavascriptFunction(const std::wstring& function_name,
                              const Value* value);

 private:
  DISALLOW_COPY_AND_ASSIGN(GpuMessageHandler);
};

////////////////////////////////////////////////////////////////////////////////
//
// GpuHTMLSource
//
////////////////////////////////////////////////////////////////////////////////

GpuHTMLSource::GpuHTMLSource()
    : DataSource(chrome::kChromeUIGpuInternalsHost, MessageLoop::current()) {
}

void GpuHTMLSource::StartDataRequest(const std::string& path,
    bool is_off_the_record,
    int request_id) {
  DictionaryValue localized_strings;
  SetFontAndTextDirection(&localized_strings);

  base::StringPiece gpu_html(
      ResourceBundle::GetSharedInstance().GetRawDataResource(
          IDR_GPU_INTERNALS_HTML));
  std::string full_html(gpu_html.data(), gpu_html.size());
  jstemplate_builder::AppendJsonHtml(&localized_strings, &full_html);
  jstemplate_builder::AppendI18nTemplateSourceHtml(&full_html);
  jstemplate_builder::AppendI18nTemplateProcessHtml(&full_html);
  jstemplate_builder::AppendJsTemplateSourceHtml(&full_html);


  scoped_refptr<RefCountedBytes> html_bytes(new RefCountedBytes);
  html_bytes->data.resize(full_html.size());
  std::copy(full_html.begin(), full_html.end(), html_bytes->data.begin());

  SendResponse(request_id, html_bytes);
}

std::string GpuHTMLSource::GetMimeType(const std::string&) const {
  return "text/html";
}

////////////////////////////////////////////////////////////////////////////////
//
// GpuMessageHandler
//
////////////////////////////////////////////////////////////////////////////////

GpuMessageHandler::GpuMessageHandler() {
}

GpuMessageHandler::~GpuMessageHandler() {}

DOMMessageHandler* GpuMessageHandler::Attach(DOMUI* dom_ui) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DOMMessageHandler* result = DOMMessageHandler::Attach(dom_ui);
  return result;
}

/* BrowserBridge.callAsync prepends a requestID to these messages. */
void GpuMessageHandler::RegisterMessages() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  dom_ui_->RegisterMessageCallback(
      "callAsync",
      NewCallback(this, &GpuMessageHandler::OnCallAsync));
}

void GpuMessageHandler::OnCallAsync(const ListValue* args) {
  DCHECK_GE(args->GetSize(), static_cast<size_t>(2));
  // unpack args into requestId, submessage and submessageArgs
  bool ok;
  Value* requestId;
  ok = args->Get(0, &requestId);
  DCHECK(ok);

  std::string submessage;
  ok = args->GetString(1, &submessage);
  DCHECK(ok);

  ListValue* submessageArgs = new ListValue();
  for (size_t i = 2; i < args->GetSize(); ++i) {
    Value* arg;
    ok = args->Get(i, &arg);
    DCHECK(ok);

    Value* argCopy = arg->DeepCopy();
    submessageArgs->Append(argCopy);
  }

  // call the submessage handler
  Value* ret = NULL;
  if (submessage == "requestGpuInfo") {
    ret = OnRequestGpuInfo(submessageArgs);
  } else if (submessage == "requestClientInfo") {
    ret = OnRequestClientInfo(submessageArgs);
  } else {  // unrecognized submessage
    NOTREACHED();
    delete submessageArgs;
    return;
  }
  delete submessageArgs;

  // call BrowserBridge.onCallAsyncReply with result
  if (ret) {
    dom_ui_->CallJavascriptFunction(L"browserBridge.onCallAsyncReply",
        *requestId,
        *ret);
    delete ret;
  } else {
    dom_ui_->CallJavascriptFunction(L"browserBridge.onCallAsyncReply",
        *requestId);
  }
}

Value* GpuMessageHandler::OnRequestClientInfo(const ListValue* list) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  DictionaryValue* dict = new DictionaryValue();

  chrome::VersionInfo version_info;

  if (!version_info.is_valid()) {
    DLOG(ERROR) << "Unable to create chrome::VersionInfo";
  } else {
    // We have everything we need to send the right values.
    dict->SetString("version", version_info.Version());
    dict->SetString("cl", version_info.LastChange());
    dict->SetString("version_mod",
      platform_util::GetVersionStringModifier());
    dict->SetString("official",
      l10n_util::GetStringUTF16(
      version_info.IsOfficialBuild() ?
      IDS_ABOUT_VERSION_OFFICIAL
      : IDS_ABOUT_VERSION_UNOFFICIAL));

    dict->SetString("command_line",
      CommandLine::ForCurrentProcess()->command_line_string());
  }

  return dict;
}

DictionaryValue* NewDescriptionValuePair(const std::string& desc,
    const std::string& value) {
  DictionaryValue* dict = new DictionaryValue();
  dict->SetString("description", desc);
  dict->SetString("value", value);
  return dict;
}

DictionaryValue* NewDescriptionValuePair(const std::string& desc,
    Value* value) {
  DictionaryValue* dict = new DictionaryValue();
  dict->SetString("description", desc);
  dict->Set("value", value);
  return dict;
}

#if defined(OS_WIN)
// Output DxDiagNode tree as nested array of {description,value} pairs
ListValue* DxDiagNodeToList(const DxDiagNode& node) {
  ListValue* list = new ListValue();
  for (std::map<std::string, std::string>::const_iterator it =
      node.values.begin();
      it != node.values.end();
      ++it) {
    list->Append(NewDescriptionValuePair(it->first, it->second));
  }

  for (std::map<std::string, DxDiagNode>::const_iterator it =
      node.children.begin();
      it != node.children.end();
      ++it) {
    ListValue* sublist = DxDiagNodeToList(it->second);
    list->Append(NewDescriptionValuePair(it->first, sublist));
  }
  return list;
}

#endif  // OS_WIN

std::string VersionNumberToString(uint32 value) {
  int hi = (value >> 8) & 0xff;
  int low = value & 0xff;
  return base::IntToString(hi) + "." + base::IntToString(low);
}

DictionaryValue* GpuInfoToDict(const GPUInfo& gpu_info) {
  ListValue* basic_info = new ListValue();
  basic_info->Append(NewDescriptionValuePair("Initialization time",
      base::Int64ToString(gpu_info.initialization_time().InMilliseconds())));
  basic_info->Append(NewDescriptionValuePair("Vendor Id",
      base::StringPrintf("0x%04x", gpu_info.vendor_id())));
  basic_info->Append(NewDescriptionValuePair("Device Id",
      base::StringPrintf("0x%04x", gpu_info.device_id())));
  basic_info->Append(NewDescriptionValuePair("Driver version",
      WideToASCII(gpu_info.driver_version()).c_str()));
  basic_info->Append(NewDescriptionValuePair("Pixel shader version",
      VersionNumberToString(gpu_info.pixel_shader_version())));
  basic_info->Append(NewDescriptionValuePair("Vertex shader version",
      VersionNumberToString(gpu_info.vertex_shader_version())));
  basic_info->Append(NewDescriptionValuePair("GL version",
      VersionNumberToString(gpu_info.gl_version())));

  DictionaryValue* info = new DictionaryValue();
  info->Set("basic_info", basic_info);

  if (gpu_info.progress() == GPUInfo::kPartial) {
    info->SetString("progress", "partial");
  } else {
    info->SetString("progress", "complete");
  }
#if defined(OS_WIN)
  if (gpu_info.progress() == GPUInfo::kComplete) {
    ListValue* dx_info = DxDiagNodeToList(gpu_info.dx_diagnostics());
    info->Set("diagnostics", dx_info);
  }
#endif

  return info;
}

Value* GpuMessageHandler::OnRequestGpuInfo(const ListValue* list) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // Get GPU Info.
  GPUInfo gpu_info = GpuProcessHostUIShim::GetInstance()->gpu_info();

  std::string html;
  if (gpu_info.progress() != GPUInfo::kComplete) {
    GpuProcessHostUIShim::GetInstance()->CollectGraphicsInfoAsynchronously();
  }

  if (gpu_info.progress() != GPUInfo::kUninitialized) {
    return GpuInfoToDict(gpu_info);
  } else {
    return NULL;
  }
}

}  // namespace


////////////////////////////////////////////////////////////////////////////////
//
// GpuInternalsUI
//
////////////////////////////////////////////////////////////////////////////////

GpuInternalsUI::GpuInternalsUI(TabContents* contents) : DOMUI(contents) {
  AddMessageHandler((new GpuMessageHandler())->Attach(this));

  GpuHTMLSource* html_source = new GpuHTMLSource();

  // Set up the chrome://gpu/ source.
  BrowserThread::PostTask(
      BrowserThread::IO, FROM_HERE,
      NewRunnableMethod(
          ChromeURLDataManager::GetInstance(),
          &ChromeURLDataManager::AddDataSource,
          make_scoped_refptr(html_source)));
}

