// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/proxy/init_proxy_resolver.h"

#include "base/compiler_specific.h"
#include "base/format_macros.h"
#include "base/logging.h"
#include "base/string_util.h"
#include "net/base/net_log.h"
#include "net/base/net_errors.h"
#include "net/proxy/proxy_config.h"
#include "net/proxy/proxy_resolver.h"
#include "net/proxy/proxy_script_fetcher.h"

namespace net {

InitProxyResolver::InitProxyResolver(ProxyResolver* resolver,
                                     ProxyScriptFetcher* proxy_script_fetcher,
                                     NetLog* net_log)
    : resolver_(resolver),
      proxy_script_fetcher_(proxy_script_fetcher),
      ALLOW_THIS_IN_INITIALIZER_LIST(io_callback_(
          this, &InitProxyResolver::OnIOCompletion)),
      user_callback_(NULL),
      current_pac_url_index_(0u),
      next_state_(STATE_NONE),
      net_log_(BoundNetLog::Make(
          net_log, NetLog::SOURCE_INIT_PROXY_RESOLVER)) {
}

InitProxyResolver::~InitProxyResolver() {
  if (next_state_ != STATE_NONE)
    Cancel();
}

int InitProxyResolver::Init(const ProxyConfig& config,
                            CompletionCallback* callback) {
  DCHECK_EQ(STATE_NONE, next_state_);
  DCHECK(callback);
  DCHECK(config.MayRequirePACResolver());

  net_log_.BeginEvent(NetLog::TYPE_INIT_PROXY_RESOLVER, NULL);

  pac_urls_ = BuildPacUrlsFallbackList(config);
  DCHECK(!pac_urls_.empty());

  next_state_ = GetStartState();

  int rv = DoLoop(OK);
  if (rv == ERR_IO_PENDING)
    user_callback_ = callback;
  else
    DidCompleteInit();

  return rv;
}

// Initialize the fallback rules.
// (1) WPAD
// (2) Custom PAC URL.
InitProxyResolver::UrlList InitProxyResolver::BuildPacUrlsFallbackList(
    const ProxyConfig& config) const {
  UrlList pac_urls;
  if (config.auto_detect())
    pac_urls.push_back(PacURL(true, GURL()));
  if (config.has_pac_url())
    pac_urls.push_back(PacURL(false, config.pac_url()));
  return pac_urls;
}

void InitProxyResolver::OnIOCompletion(int result) {
  DCHECK_NE(STATE_NONE, next_state_);
  int rv = DoLoop(result);
  if (rv != ERR_IO_PENDING) {
    DidCompleteInit();
    DoCallback(rv);
  }
}

int InitProxyResolver::DoLoop(int result) {
  DCHECK_NE(next_state_, STATE_NONE);
  int rv = result;
  do {
    State state = next_state_;
    next_state_ = STATE_NONE;
    switch (state) {
      case STATE_FETCH_PAC_SCRIPT:
        DCHECK_EQ(OK, rv);
        rv = DoFetchPacScript();
        break;
      case STATE_FETCH_PAC_SCRIPT_COMPLETE:
        rv = DoFetchPacScriptComplete(rv);
        break;
      case STATE_SET_PAC_SCRIPT:
        DCHECK_EQ(OK, rv);
        rv = DoSetPacScript();
        break;
      case STATE_SET_PAC_SCRIPT_COMPLETE:
        rv = DoSetPacScriptComplete(rv);
        break;
      default:
        NOTREACHED() << "bad state";
        rv = ERR_UNEXPECTED;
        break;
    }
  } while (rv != ERR_IO_PENDING && next_state_ != STATE_NONE);
  return rv;
}

void InitProxyResolver::DoCallback(int result) {
  DCHECK_NE(ERR_IO_PENDING, result);
  DCHECK(user_callback_);
  user_callback_->Run(result);
}

int InitProxyResolver::DoFetchPacScript() {
  DCHECK(resolver_->expects_pac_bytes());

  next_state_ = STATE_FETCH_PAC_SCRIPT_COMPLETE;

  const PacURL& pac_url = current_pac_url();

  const GURL effective_pac_url =
      pac_url.auto_detect ? GURL("http://wpad/wpad.dat") : pac_url.url;

  net_log_.BeginEvent(
      NetLog::TYPE_INIT_PROXY_RESOLVER_FETCH_PAC_SCRIPT,
      new NetLogStringParameter("url",
                                effective_pac_url.possibly_invalid_spec()));

  if (!proxy_script_fetcher_) {
    net_log_.AddEvent(NetLog::TYPE_INIT_PROXY_RESOLVER_HAS_NO_FETCHER, NULL);
    return ERR_UNEXPECTED;
  }

  return proxy_script_fetcher_->Fetch(effective_pac_url,
                                      &pac_script_,
                                      &io_callback_);
}

int InitProxyResolver::DoFetchPacScriptComplete(int result) {
  DCHECK(resolver_->expects_pac_bytes());

  if (result == OK) {
    net_log_.EndEvent(NetLog::TYPE_INIT_PROXY_RESOLVER_FETCH_PAC_SCRIPT, NULL);
  } else {
    net_log_.EndEvent(
        NetLog::TYPE_INIT_PROXY_RESOLVER_FETCH_PAC_SCRIPT,
        new NetLogIntegerParameter("net_error", result));
    return TryToFallbackPacUrl(result);
  }

  next_state_ = STATE_SET_PAC_SCRIPT;
  return result;
}

int InitProxyResolver::DoSetPacScript() {
  net_log_.BeginEvent(NetLog::TYPE_INIT_PROXY_RESOLVER_SET_PAC_SCRIPT, NULL);

  const PacURL& pac_url = current_pac_url();

  next_state_ = STATE_SET_PAC_SCRIPT_COMPLETE;

  scoped_refptr<ProxyResolverScriptData> script_data;

  if (resolver_->expects_pac_bytes()) {
    script_data = ProxyResolverScriptData::FromUTF16(pac_script_);
  } else {
    script_data = pac_url.auto_detect ?
        ProxyResolverScriptData::ForAutoDetect() :
        ProxyResolverScriptData::FromURL(pac_url.url);
  }

  return resolver_->SetPacScript(script_data, &io_callback_);
}

int InitProxyResolver::DoSetPacScriptComplete(int result) {
  if (result != OK) {
    net_log_.EndEvent(
        NetLog::TYPE_INIT_PROXY_RESOLVER_SET_PAC_SCRIPT,
        new NetLogIntegerParameter("net_error", result));
    return TryToFallbackPacUrl(result);
  }

  net_log_.EndEvent(NetLog::TYPE_INIT_PROXY_RESOLVER_SET_PAC_SCRIPT, NULL);
  return result;
}

int InitProxyResolver::TryToFallbackPacUrl(int error) {
  DCHECK_LT(error, 0);

  if (current_pac_url_index_ + 1 >= pac_urls_.size()) {
    // Nothing left to fall back to.
    return error;
  }

  // Advance to next URL in our list.
  ++current_pac_url_index_;

  net_log_.AddEvent(
      NetLog::TYPE_INIT_PROXY_RESOLVER_FALLING_BACK_TO_NEXT_PAC_URL, NULL);

  next_state_ = GetStartState();

  return OK;
}

InitProxyResolver::State InitProxyResolver::GetStartState() const {
  return resolver_->expects_pac_bytes() ?
      STATE_FETCH_PAC_SCRIPT : STATE_SET_PAC_SCRIPT;
}

const InitProxyResolver::PacURL& InitProxyResolver::current_pac_url() const {
  DCHECK_LT(current_pac_url_index_, pac_urls_.size());
  return pac_urls_[current_pac_url_index_];
}

void InitProxyResolver::DidCompleteInit() {
  net_log_.EndEvent(NetLog::TYPE_INIT_PROXY_RESOLVER, NULL);
}

void InitProxyResolver::Cancel() {
  DCHECK_NE(STATE_NONE, next_state_);

  net_log_.AddEvent(NetLog::TYPE_CANCELLED, NULL);

  switch (next_state_) {
    case STATE_FETCH_PAC_SCRIPT_COMPLETE:
      proxy_script_fetcher_->Cancel();
      break;
    case STATE_SET_PAC_SCRIPT_COMPLETE:
      resolver_->CancelSetPacScript();
      break;
    default:
      NOTREACHED();
      break;
  }

  DidCompleteInit();
}

}  // namespace net
