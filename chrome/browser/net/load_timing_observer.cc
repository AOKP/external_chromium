// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/net/load_timing_observer.h"

#include "base/compiler_specific.h"
#include "base/time.h"
#include "net/base/load_flags.h"
#include "net/url_request/url_request_netlog_params.h"

using base::Time;
using base::TimeTicks;
using webkit_glue::ResourceLoaderBridge;

const size_t kMaxNumEntries = 1000;

namespace {

// We know that this conversion is not solid and suffers from world clock
// changes, but it should be good enough for the load timing info.
static Time TimeTicksToTime(const TimeTicks& time_ticks) {
  static int64 tick_to_time_offset;
  static bool tick_to_time_offset_available = false;
  if (!tick_to_time_offset_available) {
    int64 cur_time = (Time::Now() - Time()).InMicroseconds();
    int64 cur_time_ticks = (TimeTicks::Now() - TimeTicks()).InMicroseconds();
    // If we add this number to a time tick value, it gives the timestamp.
    tick_to_time_offset = cur_time - cur_time_ticks;
    tick_to_time_offset_available = true;
  }
  return Time::FromInternalValue(time_ticks.ToInternalValue() +
                                 tick_to_time_offset);
}

static int32 TimeTicksToOffset(
    const TimeTicks& time_ticks,
    LoadTimingObserver::URLRequestRecord* record) {
  return static_cast<int32>(
      (time_ticks - record->base_ticks).InMillisecondsRoundedUp());
}

}

LoadTimingObserver::URLRequestRecord::URLRequestRecord()
    : connect_job_id(net::NetLog::Source::kInvalidId),
      socket_log_id(net::NetLog::Source::kInvalidId),
      socket_reused(false) {
}

LoadTimingObserver::LoadTimingObserver() {
}

LoadTimingObserver::~LoadTimingObserver() {
}

LoadTimingObserver::URLRequestRecord*
LoadTimingObserver::GetURLRequestRecord(uint32 source_id) {
  URLRequestToRecordMap::iterator it = url_request_to_record_.find(source_id);
  if (it != url_request_to_record_.end())
    return &it->second;
  return NULL;
}

void LoadTimingObserver::OnAddEntry(net::NetLog::EventType type,
                                    const base::TimeTicks& time,
                                    const net::NetLog::Source& source,
                                    net::NetLog::EventPhase phase,
                                    net::NetLog::EventParameters* params) {
  if (source.type == net::NetLog::SOURCE_URL_REQUEST)
    OnAddURLRequestEntry(type, time, source, phase, params);
  else if (source.type == net::NetLog::SOURCE_CONNECT_JOB)
    OnAddConnectJobEntry(type, time, source, phase, params);
  else if (source.type == net::NetLog::SOURCE_SOCKET)
    OnAddSocketEntry(type, time, source, phase, params);
}

void LoadTimingObserver::OnAddURLRequestEntry(
    net::NetLog::EventType type,
    const base::TimeTicks& time,
    const net::NetLog::Source& source,
    net::NetLog::EventPhase phase,
    net::NetLog::EventParameters* params) {
  bool is_begin = phase == net::NetLog::PHASE_BEGIN;
  bool is_end = phase == net::NetLog::PHASE_END;

  if (type == net::NetLog::TYPE_URL_REQUEST_START_JOB) {
    if (is_begin) {
      // Only record timing for entries with corresponding flag.
      int load_flags = static_cast<URLRequestStartEventParameters*>(params)->
          load_flags();
      if (!(load_flags & net::LOAD_ENABLE_LOAD_TIMING))
        return;

      // Prevents us from passively growing the memory memory unbounded in case
      // something went wrong. Should not happen.
      if (url_request_to_record_.size() > kMaxNumEntries) {
        LOG(WARNING) << "The load timing observer url request count has grown "
                        "larger than expected, resetting";
        url_request_to_record_.clear();
      }

      URLRequestRecord& record = url_request_to_record_[source.id];
      record.base_ticks = time;
      record.timing.base_time = TimeTicksToTime(time);
    }
    return;
  } else if (type == net::NetLog::TYPE_REQUEST_ALIVE) {
    // Cleanup records based on the TYPE_REQUEST_ALIVE entry.
    if (is_end)
      url_request_to_record_.erase(source.id);
    return;
  }

  URLRequestRecord* record = GetURLRequestRecord(source.id);
  if (!record)
    return;

  ResourceLoaderBridge::LoadTimingInfo& timing = record->timing;

  switch(type) {
    case net::NetLog::TYPE_PROXY_SERVICE:
      if (is_begin)
        timing.proxy_start = TimeTicksToOffset(time, record);
      else if (is_end)
        timing.proxy_end = TimeTicksToOffset(time, record);
      break;
    case net::NetLog::TYPE_SOCKET_POOL:
      if (is_begin)
        timing.connect_start = TimeTicksToOffset(time, record);
      else if (is_end)
        timing.connect_end = TimeTicksToOffset(time, record);
      break;
    case net::NetLog::TYPE_SOCKET_POOL_BOUND_TO_CONNECT_JOB:
      {
        uint32 connect_job_id = static_cast<net::NetLogSourceParameter*>(
            params)->value().id;
        ConnectJobToRecordMap::iterator it =
            connect_job_to_record_.find(connect_job_id);
        if (it != connect_job_to_record_.end() &&
                !it->second.dns_start.is_null()) {
          timing.dns_start = TimeTicksToOffset(it->second.dns_start, record);
          timing.dns_end = TimeTicksToOffset(it->second.dns_end, record);
        }
      }
      break;
    case net::NetLog::TYPE_SOCKET_POOL_REUSED_AN_EXISTING_SOCKET:
      record->socket_reused = true;
      break;
    case net::NetLog::TYPE_SOCKET_POOL_BOUND_TO_SOCKET:
      record->socket_log_id = static_cast<net::NetLogSourceParameter*>(
          params)->value().id;
      if (!record->socket_reused) {
        SocketToRecordMap::iterator it =
            socket_to_record_.find(record->socket_log_id);
        if (it != socket_to_record_.end() && !it->second.ssl_start.is_null()) {
            timing.ssl_start = TimeTicksToOffset(it->second.ssl_start, record);
            timing.ssl_end = TimeTicksToOffset(it->second.ssl_end, record);
        }
      }
      break;
    case net::NetLog::TYPE_HTTP_TRANSACTION_SEND_REQUEST:
    case net::NetLog::TYPE_SPDY_TRANSACTION_SEND_REQUEST:
      if (is_begin)
        timing.send_start = TimeTicksToOffset(time, record);
      else if (is_end)
        timing.send_end = TimeTicksToOffset(time, record);
      break;
    case net::NetLog::TYPE_HTTP_TRANSACTION_READ_HEADERS:
    case net::NetLog::TYPE_SPDY_TRANSACTION_READ_HEADERS:
      if (is_begin)
        timing.receive_headers_start =  TimeTicksToOffset(time, record);
      else if (is_end)
        timing.receive_headers_end =  TimeTicksToOffset(time, record);
      break;
    default:
      break;
  }
}

void LoadTimingObserver::OnAddConnectJobEntry(
    net::NetLog::EventType type,
    const base::TimeTicks& time,
    const net::NetLog::Source& source,
    net::NetLog::EventPhase phase,
    net::NetLog::EventParameters* params) {
  bool is_begin = phase == net::NetLog::PHASE_BEGIN;
  bool is_end = phase == net::NetLog::PHASE_END;

  // Manage record lifetime based on the SOCKET_POOL_CONNECT_JOB entry.
  if (type == net::NetLog::TYPE_SOCKET_POOL_CONNECT_JOB) {
    if (is_begin) {
      // Prevents us from passively growing the memory memory unbounded in case
      // something went wrong. Should not happen.
      if (connect_job_to_record_.size() > kMaxNumEntries) {
        LOG(WARNING) << "The load timing observer connect job count has grown "
                        "larger than expected, resetting";
        connect_job_to_record_.clear();
      }

      connect_job_to_record_.insert(
          std::make_pair(source.id, ConnectJobRecord()));
    } else if (is_end) {
      connect_job_to_record_.erase(source.id);
    }
  } else if (type == net::NetLog::TYPE_HOST_RESOLVER_IMPL) {
    ConnectJobToRecordMap::iterator it =
        connect_job_to_record_.find(source.id);
    if (it != connect_job_to_record_.end()) {
      if (is_begin)
        it->second.dns_start = time;
      else if (is_end)
        it->second.dns_end = time;
    }
  }
}

void LoadTimingObserver::OnAddSocketEntry(
    net::NetLog::EventType type,
    const base::TimeTicks& time,
    const net::NetLog::Source& source,
    net::NetLog::EventPhase phase,
    net::NetLog::EventParameters* params) {
  bool is_begin = phase == net::NetLog::PHASE_BEGIN;
  bool is_end = phase == net::NetLog::PHASE_END;

  // Manage record lifetime based on the SOCKET_ALIVE entry.
  if (type == net::NetLog::TYPE_SOCKET_ALIVE) {
    if (is_begin) {
      // Prevents us from passively growing the memory memory unbounded in case
      // something went wrong. Should not happen.
      if (socket_to_record_.size() > kMaxNumEntries) {
        LOG(WARNING) << "The load timing observer socket count has grown "
                        "larger than expected, resetting";
        socket_to_record_.clear();
      }

      socket_to_record_.insert(
          std::make_pair(source.id, SocketRecord()));
    } else if (is_end) {
      socket_to_record_.erase(source.id);
    }
    return;
  }
  SocketToRecordMap::iterator it = socket_to_record_.find(source.id);
  if (it == socket_to_record_.end())
    return;

  if (type == net::NetLog::TYPE_SSL_CONNECT) {
    if (is_begin)
      it->second.ssl_start = time;
    else if (is_end)
      it->second.ssl_end = time;
  }
}
