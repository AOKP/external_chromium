// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/dom_ui/dom_ui.h"

#include "base/i18n/rtl.h"
#include "base/json/json_writer.h"
#include "base/stl_util-inl.h"
#include "base/string_util.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_theme_provider.h"
#include "chrome/browser/profile.h"
#include "chrome/browser/renderer_host/render_view_host.h"
#include "chrome/browser/tab_contents/tab_contents.h"
#include "chrome/browser/tab_contents/tab_contents_view.h"
#include "chrome/common/bindings_policy.h"

DOMUI::DOMUI(TabContents* contents)
    : hide_favicon_(false),
      force_bookmark_bar_visible_(false),
      force_extension_shelf_visible_(false),
      focus_location_bar_by_default_(false),
      should_hide_url_(false),
      link_transition_type_(PageTransition::LINK),
      bindings_(BindingsPolicy::DOM_UI),
      tab_contents_(contents) {
}

DOMUI::~DOMUI() {
  STLDeleteContainerPairSecondPointers(message_callbacks_.begin(),
                                       message_callbacks_.end());
  STLDeleteContainerPointers(handlers_.begin(), handlers_.end());
}

// DOMUI, public: -------------------------------------------------------------

void DOMUI::ProcessDOMUIMessage(const std::string& message,
                                const ListValue* content,
                                const GURL& source_url,
                                int request_id,
                                bool has_callback) {
  // Look up the callback for this message.
  MessageCallbackMap::const_iterator callback =
      message_callbacks_.find(message);
  if (callback == message_callbacks_.end())
    return;

  // Forward this message and content on.
  callback->second->Run(content);
}

void DOMUI::CallJavascriptFunction(const std::wstring& function_name) {
  std::wstring javascript = function_name + L"();";
  ExecuteJavascript(javascript);
}

void DOMUI::CallJavascriptFunction(const std::wstring& function_name,
                                   const Value& arg) {
  std::string json;
  base::JSONWriter::Write(&arg, false, &json);
  std::wstring javascript = function_name + L"(" + UTF8ToWide(json) + L");";

  ExecuteJavascript(javascript);
}

void DOMUI::CallJavascriptFunction(
    const std::wstring& function_name,
    const Value& arg1, const Value& arg2) {
  std::string json;
  base::JSONWriter::Write(&arg1, false, &json);
  std::wstring javascript = function_name + L"(" + UTF8ToWide(json);
  base::JSONWriter::Write(&arg2, false, &json);
  javascript += L"," + UTF8ToWide(json) + L");";

  ExecuteJavascript(javascript);
}

ThemeProvider* DOMUI::GetThemeProvider() const {
  return tab_contents_->profile()->GetThemeProvider();
}

void DOMUI::RegisterMessageCallback(const std::string &message,
                                    MessageCallback *callback) {
  message_callbacks_.insert(std::make_pair(message, callback));
}

Profile* DOMUI::GetProfile() {
  return tab_contents()->profile();
}

// DOMUI, protected: ----------------------------------------------------------

void DOMUI::AddMessageHandler(DOMMessageHandler* handler) {
  handlers_.push_back(handler);
}

// DOMUI, private: ------------------------------------------------------------

void DOMUI::ExecuteJavascript(const std::wstring& javascript) {
  tab_contents()->render_view_host()->ExecuteJavascriptInWebFrame(
      std::wstring(), javascript);
}

///////////////////////////////////////////////////////////////////////////////
// DOMMessageHandler

DOMMessageHandler* DOMMessageHandler::Attach(DOMUI* dom_ui) {
  dom_ui_ = dom_ui;
  RegisterMessages();
  return this;
}

// DOMMessageHandler, protected: ----------------------------------------------

void DOMMessageHandler::SetURLAndTitle(DictionaryValue* dictionary,
                                       string16 title,
                                       const GURL& gurl) {
  string16 url16 = UTF8ToUTF16(gurl.spec());
  dictionary->SetStringFromUTF16(L"url", url16);

  bool using_url_as_the_title = false;
  if (title.empty()) {
    using_url_as_the_title = true;
    title = url16;
  }

  // Since the title can contain BiDi text, we need to mark the text as either
  // RTL or LTR, depending on the characters in the string. If we use the URL
  // as the title, we mark the title as LTR since URLs are always treated as
  // left to right strings.
  string16 title_to_set(title);
  if (base::i18n::IsRTL()) {
    if (using_url_as_the_title) {
      base::i18n::WrapStringWithLTRFormatting(&title_to_set);
    } else {
      bool success =
          base::i18n::AdjustStringForLocaleDirection(title, &title_to_set);
      DCHECK(success ? (title != title_to_set) : (title == title_to_set));
    }
  }
  dictionary->SetStringFromUTF16(L"title", title_to_set);
}

bool DOMMessageHandler::ExtractIntegerValue(const Value* value, int* out_int) {
  if (value && value->GetType() == Value::TYPE_LIST) {
    const ListValue* list_value = static_cast<const ListValue*>(value);
    std::string string_value;
    if (list_value->GetString(0, &string_value)) {
      *out_int = StringToInt(string_value);
      return true;
    }
  }
  return false;
}

std::wstring DOMMessageHandler::ExtractStringValue(const Value* value) {
  if (value && value->GetType() == Value::TYPE_LIST) {
    const ListValue* list_value = static_cast<const ListValue*>(value);
    std::wstring wstring_value;
    if (list_value->GetString(0, &wstring_value))
      return wstring_value;
  }
  return std::wstring();
}
