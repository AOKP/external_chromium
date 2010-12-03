// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/tab_contents/tab_contents_delegate.h"

#include "chrome/browser/search_engines/template_url.h"
#include "gfx/rect.h"

void TabContentsDelegate::DetachContents(TabContents* source) {
}

bool TabContentsDelegate::IsPopup(const TabContents* source) const {
  return false;
}

TabContents* TabContentsDelegate::GetConstrainingContents(TabContents* source) {
  return source;
}

bool TabContentsDelegate::ShouldFocusConstrainedWindow() {
  return true;
}

void TabContentsDelegate::WillShowConstrainedWindow(TabContents* source) {
}

void TabContentsDelegate::ContentsMouseEvent(
    TabContents* source, const gfx::Point& location, bool motion) {
}

void TabContentsDelegate::ContentsZoomChange(bool zoom_in) { }

void TabContentsDelegate::OnContentSettingsChange(TabContents* source) { }

bool TabContentsDelegate::IsApplication() const { return false; }

void TabContentsDelegate::ConvertContentsToApplication(TabContents* source) { }

bool TabContentsDelegate::CanReloadContents(TabContents* source) const {
  return true;
}

void TabContentsDelegate::ShowHtmlDialog(HtmlDialogUIDelegate* delegate,
                                         gfx::NativeWindow parent_window) {
}

void TabContentsDelegate::BeforeUnloadFired(TabContents* tab,
                                            bool proceed,
                                            bool* proceed_to_fire_unload) {
  *proceed_to_fire_unload = true;
}

void TabContentsDelegate::ForwardMessageToExternalHost(
    const std::string& message,
    const std::string& origin,
    const std::string& target) {
}

bool TabContentsDelegate::IsExternalTabContainer() const { return false; }

void TabContentsDelegate::SetFocusToLocationBar(bool select_all) {}

bool TabContentsDelegate::ShouldFocusPageAfterCrash() {
  return true;
}

void TabContentsDelegate::RenderWidgetShowing() {}

bool TabContentsDelegate::TakeFocus(bool reverse) {
  return false;
}

void TabContentsDelegate::LostCapture() {
}

void TabContentsDelegate::SetTabContentBlocked(
    TabContents* contents, bool blocked) {
}

void TabContentsDelegate::TabContentsFocused(TabContents* tab_content) {
}

int TabContentsDelegate::GetExtraRenderViewHeight() const {
  return 0;
}

bool TabContentsDelegate::CanDownload(int request_id) {
  return true;
}

void TabContentsDelegate::OnStartDownload(DownloadItem* download,
                                          TabContents* tab) {
}

bool TabContentsDelegate::HandleContextMenu(const ContextMenuParams& params) {
  return false;
}

bool TabContentsDelegate::ExecuteContextMenuCommand(int command) {
  return false;
}

void TabContentsDelegate::ConfirmSetDefaultSearchProvider(
    TabContents* tab_contents,
    TemplateURL* template_url,
    TemplateURLModel* template_url_model) {
  delete template_url;
}

void TabContentsDelegate::ConfirmAddSearchProvider(
    const TemplateURL* template_url,
    Profile* profile) {
  delete template_url;
}

void TabContentsDelegate::ShowPageInfo(Profile* profile,
                                       const GURL& url,
                                       const NavigationEntry::SSLStatus& ssl,
                                       bool show_history) {
}

bool TabContentsDelegate::PreHandleKeyboardEvent(
    const NativeWebKeyboardEvent& event,
    bool* is_keyboard_shortcut) {
  return false;
}

void TabContentsDelegate::HandleKeyboardEvent(
    const NativeWebKeyboardEvent& event) {
}

void TabContentsDelegate::HandleMouseUp() {
}

void TabContentsDelegate::HandleMouseActivate() {
}

void TabContentsDelegate::ShowRepostFormWarningDialog(
    TabContents* tab_contents) {
}

void TabContentsDelegate::ShowContentSettingsWindow(
    ContentSettingsType content_type) {
}

void TabContentsDelegate::ShowCollectedCookiesDialog(
    TabContents* tab_contents) {
}

bool TabContentsDelegate::OnGoToEntryOffset(int offset) {
  return true;
}

bool TabContentsDelegate::ShouldAddNavigationToHistory(
    const history::HistoryAddPageArgs& add_page_args,
    NavigationType::Type navigation_type) {
  return true;
}

void TabContentsDelegate::OnDidGetApplicationInfo(TabContents* tab_contents,
                                                  int32 page_id) {
}

gfx::NativeWindow TabContentsDelegate::GetFrameNativeWindow() {
  return NULL;
}

void TabContentsDelegate::TabContentsCreated(TabContents* new_contents) {
}

bool TabContentsDelegate::infobars_enabled() {
  return true;
}

bool TabContentsDelegate::ShouldEnablePreferredSizeNotifications() {
  return false;
}

void TabContentsDelegate::UpdatePreferredSize(const gfx::Size& pref_size) {
}

void TabContentsDelegate::OnSetSuggestions(
    int32 page_id,
    const std::vector<std::string>& suggestions) {
}

void TabContentsDelegate::OnInstantSupportDetermined(int32 page_id,
                                                     bool result) {
}

void TabContentsDelegate::ContentRestrictionsChanged(TabContents* source) {
}

TabContentsDelegate::~TabContentsDelegate() {
}
