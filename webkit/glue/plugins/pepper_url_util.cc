// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/glue/plugins/pepper_url_util.h"

#include "googleurl/src/gurl.h"
#include "ppapi/c/dev/ppb_url_util_dev.h"
#include "third_party/WebKit/WebKit/chromium/public/WebDocument.h"
#include "third_party/WebKit/WebKit/chromium/public/WebElement.h"
#include "third_party/WebKit/WebKit/chromium/public/WebFrame.h"
#include "third_party/WebKit/WebKit/chromium/public/WebNode.h"
#include "third_party/WebKit/WebKit/chromium/public/WebPluginContainer.h"
#include "third_party/WebKit/WebKit/chromium/public/WebSecurityOrigin.h"
#include "third_party/WebKit/WebKit/chromium/public/WebURL.h"
#include "webkit/glue/plugins/pepper_common.h"
#include "webkit/glue/plugins/pepper_plugin_instance.h"
#include "webkit/glue/plugins/pepper_string.h"
#include "webkit/glue/plugins/pepper_var.h"

namespace pepper {

namespace {

void ConvertComponent(const url_parse::Component& input,
                      PP_UrlComponent_Dev* output) {
  output->begin = input.begin;
  output->len = input.len;
}

// Output can be NULL to specify "do nothing." This rule is followed by all the
// url util functions, so we implement it once here.
void ConvertComponents(const url_parse::Parsed& input,
                       PP_UrlComponents_Dev* output) {
  if (!output)
    return;

  ConvertComponent(input.scheme, &output->scheme);
  ConvertComponent(input.username, &output->username);
  ConvertComponent(input.password, &output->password);
  ConvertComponent(input.host, &output->host);
  ConvertComponent(input.port, &output->port);
  ConvertComponent(input.path, &output->path);
  ConvertComponent(input.query, &output->query);
  ConvertComponent(input.ref, &output->ref);
}

// Used for returning the given GURL from a PPAPI function, with an optional
// out param indicating the components.
PP_Var GenerateUrlReturn(PluginModule* module, const GURL& url,
                         PP_UrlComponents_Dev* components) {
  if (!url.is_valid())
    return PP_MakeNull();
  ConvertComponents(url.parsed_for_possibly_invalid_spec(), components);
  return StringVar::StringToPPVar(module, url.possibly_invalid_spec());
}

// Sets |*security_origin| to be the WebKit security origin associated with the
// document containing the given plugin instance. On success, returns true. If
// the instance is invalid, returns false and |*security_origin| will be
// unchanged.
bool SecurityOriginForInstance(PP_Instance instance_id,
                               WebKit::WebSecurityOrigin* security_origin) {
  PluginInstance* instance = ResourceTracker::Get()->GetInstance(instance_id);
  if (!instance)
    return false;

  WebKit::WebElement plugin_element = instance->container()->element();
  WebKit::WebFrame* plugin_frame = plugin_element.document().frame();
  if (!plugin_frame)
    return false;

  *security_origin = plugin_frame->securityOrigin();
  return true;
}

PP_Var Canonicalize(PP_Var url, PP_UrlComponents_Dev* components) {
  scoped_refptr<StringVar> url_string(StringVar::FromPPVar(url));
  if (!url_string)
    return PP_MakeNull();
  return GenerateUrlReturn(url_string->module(),
                           GURL(url_string->value()), components);
}

PP_Var ResolveRelativeToUrl(PP_Var base_url,
                            PP_Var relative,
                            PP_UrlComponents_Dev* components) {
  scoped_refptr<StringVar> base_url_string(StringVar::FromPPVar(base_url));
  scoped_refptr<StringVar> relative_string(StringVar::FromPPVar(relative));
  if (!base_url_string || !relative_string)
    return PP_MakeNull();

  GURL base_gurl(base_url_string->value());
  if (!base_gurl.is_valid())
    return PP_MakeNull();
  return GenerateUrlReturn(base_url_string->module(),
                           base_gurl.Resolve(relative_string->value()),
                           components);
}

PP_Var ResolveRelativeToDocument(PP_Instance instance_id,
                                 PP_Var relative,
                                 PP_UrlComponents_Dev* components) {
  PluginInstance* instance = ResourceTracker::Get()->GetInstance(instance_id);
  if (!instance)
    return PP_MakeNull();

  scoped_refptr<StringVar> relative_string(StringVar::FromPPVar(relative));
  if (!relative_string)
    return PP_MakeNull();

  WebKit::WebElement plugin_element = instance->container()->element();
  GURL document_url = plugin_element.document().baseURL();
  return GenerateUrlReturn(instance->module(),
                           document_url.Resolve(relative_string->value()),
                           components);
}

PP_Bool IsSameSecurityOrigin(PP_Var url_a, PP_Var url_b) {
  scoped_refptr<StringVar> url_a_string(StringVar::FromPPVar(url_a));
  scoped_refptr<StringVar> url_b_string(StringVar::FromPPVar(url_b));
  if (!url_a_string || !url_b_string)
    return PP_FALSE;

  GURL gurl_a(url_a_string->value());
  GURL gurl_b(url_b_string->value());
  if (!gurl_a.is_valid() || !gurl_b.is_valid())
    return PP_FALSE;

  return BoolToPPBool(gurl_a.GetOrigin() == gurl_b.GetOrigin());
}

PP_Bool DocumentCanRequest(PP_Instance instance, PP_Var url) {
  scoped_refptr<StringVar> url_string(StringVar::FromPPVar(url));
  if (!url_string)
    return PP_FALSE;

  WebKit::WebSecurityOrigin security_origin;
  if (!SecurityOriginForInstance(instance, &security_origin))
    return PP_FALSE;

  GURL gurl(url_string->value());
  if (!gurl.is_valid())
    return PP_FALSE;

  return BoolToPPBool(security_origin.canRequest(gurl));
}

PP_Bool DocumentCanAccessDocument(PP_Instance active, PP_Instance target) {
  WebKit::WebSecurityOrigin active_origin;
  if (!SecurityOriginForInstance(active, &active_origin))
    return PP_FALSE;

  WebKit::WebSecurityOrigin target_origin;
  if (!SecurityOriginForInstance(active, &target_origin))
    return PP_FALSE;

  return BoolToPPBool(active_origin.canAccess(target_origin));
}

}  // namespace

const PPB_UrlUtil_Dev ppb_url_util = {
  &Canonicalize,
  &ResolveRelativeToUrl,
  &ResolveRelativeToDocument,
  &IsSameSecurityOrigin,
  &DocumentCanRequest,
  &DocumentCanAccessDocument
};

// static
const PPB_UrlUtil_Dev* UrlUtil::GetInterface() {
  return &ppb_url_util;
}

}  // namespace pepper
