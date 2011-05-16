// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/favicon_service.h"

#include "chrome/browser/dom_ui/dom_ui_factory.h"
#include "chrome/browser/history/history.h"
#include "chrome/browser/history/history_backend.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/url_constants.h"

FaviconService::FaviconService(Profile* profile) : profile_(profile) {
}

FaviconService::Handle FaviconService::GetFavicon(
    const GURL& icon_url,
    CancelableRequestConsumerBase* consumer,
    FaviconDataCallback* callback) {
  GetFaviconRequest* request = new GetFaviconRequest(callback);
  AddRequest(request, consumer);
  HistoryService* hs = profile_->GetHistoryService(Profile::EXPLICIT_ACCESS);
  if (hs)
    hs->GetFavicon(request, icon_url);
  else
    ForwardEmptyResultAsync(request);
  return request->handle();
}

FaviconService::Handle FaviconService::UpdateFaviconMappingAndFetch(
    const GURL& page_url,
    const GURL& icon_url,
    CancelableRequestConsumerBase* consumer,
    FaviconService::FaviconDataCallback* callback) {
  GetFaviconRequest* request = new GetFaviconRequest(callback);
  AddRequest(request, consumer);
  HistoryService* hs = profile_->GetHistoryService(Profile::EXPLICIT_ACCESS);
  if (hs)
    hs->UpdateFaviconMappingAndFetch(request, page_url, icon_url);
  else
    ForwardEmptyResultAsync(request);
  return request->handle();
}

FaviconService::Handle FaviconService::GetFaviconForURL(
    const GURL& page_url,
    CancelableRequestConsumerBase* consumer,
    FaviconDataCallback* callback) {
  GetFaviconRequest* request = new GetFaviconRequest(callback);
  AddRequest(request, consumer);
  FaviconService::Handle handle = request->handle();
  if (page_url.SchemeIs(chrome::kChromeUIScheme) ||
      page_url.SchemeIs(chrome::kExtensionScheme)) {
    DOMUIFactory::GetFaviconForURL(profile_, request, page_url);
  } else {
    HistoryService* hs = profile_->GetHistoryService(Profile::EXPLICIT_ACCESS);
    if (hs)
      hs->GetFaviconForURL(request, page_url);
    else
      ForwardEmptyResultAsync(request);
  }
  return handle;
}

void FaviconService::SetFaviconOutOfDateForPage(const GURL& page_url) {
  HistoryService* hs = profile_->GetHistoryService(Profile::EXPLICIT_ACCESS);
  if (hs)
    hs->SetFaviconOutOfDateForPage(page_url);
}

void FaviconService::SetImportedFavicons(
    const std::vector<history::ImportedFavIconUsage>& favicon_usage) {
  HistoryService* hs = profile_->GetHistoryService(Profile::EXPLICIT_ACCESS);
  if (hs)
    hs->SetImportedFavicons(favicon_usage);
}

void FaviconService::SetFavicon(const GURL& page_url,
                                const GURL& icon_url,
                                const std::vector<unsigned char>& image_data) {
  HistoryService* hs = profile_->GetHistoryService(Profile::EXPLICIT_ACCESS);
  if (hs)
    hs->SetFavicon(page_url, icon_url, image_data);
}

FaviconService::~FaviconService() {
}

void FaviconService::ForwardEmptyResultAsync(GetFaviconRequest* request) {
  request->ForwardResultAsync(FaviconDataCallback::TupleType(request->handle(),
        false, NULL, false, GURL()));
}
