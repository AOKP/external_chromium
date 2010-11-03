// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/glue/plugins/pepper_url_response_info.h"

#include "base/logging.h"
#include "third_party/ppapi/c/pp_var.h"
#include "third_party/WebKit/WebKit/chromium/public/WebHTTPHeaderVisitor.h"
#include "third_party/WebKit/WebKit/chromium/public/WebString.h"
#include "third_party/WebKit/WebKit/chromium/public/WebURL.h"
#include "third_party/WebKit/WebKit/chromium/public/WebURLResponse.h"
#include "webkit/glue/plugins/pepper_file_ref.h"
#include "webkit/glue/plugins/pepper_var.h"
#include "webkit/glue/webkit_glue.h"

using WebKit::WebHTTPHeaderVisitor;
using WebKit::WebString;
using WebKit::WebURLResponse;

namespace pepper {

namespace {

class HeaderFlattener : public WebHTTPHeaderVisitor {
 public:
  const std::string& buffer() const { return buffer_; }

  virtual void visitHeader(const WebString& name, const WebString& value) {
    if (!buffer_.empty())
      buffer_.append("\n");
    buffer_.append(name.utf8());
    buffer_.append(": ");
    buffer_.append(value.utf8());
  }

 private:
  std::string buffer_;
};

bool IsURLResponseInfo(PP_Resource resource) {
  return !!Resource::GetAs<URLResponseInfo>(resource);
}

PP_Var GetProperty(PP_Resource response_id,
                   PP_URLResponseProperty_Dev property) {
  scoped_refptr<URLResponseInfo> response(
      Resource::GetAs<URLResponseInfo>(response_id));
  if (!response)
    return PP_MakeUndefined();

  return response->GetProperty(property);
}

PP_Resource GetBody(PP_Resource response_id) {
  scoped_refptr<URLResponseInfo> response(
      Resource::GetAs<URLResponseInfo>(response_id));
  if (!response.get())
    return 0;

  FileRef* body = response->body();
  if (!body)
    return 0;
  body->AddRef();  // AddRef for the caller.

  return body->GetReference();
}

const PPB_URLResponseInfo_Dev ppb_urlresponseinfo = {
  &IsURLResponseInfo,
  &GetProperty,
  &GetBody
};

}  // namespace

URLResponseInfo::URLResponseInfo(PluginModule* module)
    : Resource(module),
      status_code_(-1) {
}

URLResponseInfo::~URLResponseInfo() {
}

// static
const PPB_URLResponseInfo_Dev* URLResponseInfo::GetInterface() {
  return &ppb_urlresponseinfo;
}

PP_Var URLResponseInfo::GetProperty(PP_URLResponseProperty_Dev property) {
  switch (property) {
    case PP_URLRESPONSEPROPERTY_URL:
      return StringVar::StringToPPVar(module(), url_);
    case PP_URLRESPONSEPROPERTY_STATUSCODE:
      return PP_MakeInt32(status_code_);
    case PP_URLRESPONSEPROPERTY_HEADERS:
      return StringVar::StringToPPVar(module(), headers_);
    default:
      NOTIMPLEMENTED();  // TODO(darin): Implement me!
      return PP_MakeUndefined();
  }
}

bool URLResponseInfo::Initialize(const WebURLResponse& response) {
  url_ = response.url().spec();
  status_code_ = response.httpStatusCode();

  HeaderFlattener flattener;
  response.visitHTTPHeaderFields(&flattener);
  headers_ = flattener.buffer();

  WebString file_path = response.downloadFilePath();
  if (!file_path.isEmpty())
    body_ = new FileRef(module(), webkit_glue::WebStringToFilePath(file_path));
  return true;
}

}  // namespace pepper
