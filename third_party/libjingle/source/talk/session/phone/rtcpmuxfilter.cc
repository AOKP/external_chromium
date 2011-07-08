/*
 * libjingle
 * Copyright 2004--2010, Google Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "talk/session/phone/rtcpmuxfilter.h"

#include "talk/base/logging.h"

namespace cricket {

RtcpMuxFilter::RtcpMuxFilter() : state_(ST_INIT), offer_enable_(false) {
}

bool RtcpMuxFilter::IsActive() const {
  // We can receive muxed media prior to the accept, so we have to be able to
  // deal with that.
  return (state_ == ST_SENTOFFER || state_ == ST_ACTIVE);
}

bool RtcpMuxFilter::SetOffer(bool offer_enable, ContentSource source) {
  bool ret = false;
  if (state_ == ST_INIT) {
    offer_enable_ = offer_enable;
    state_ = (source == CS_LOCAL) ? ST_SENTOFFER : ST_RECEIVEDOFFER;
    ret = true;
  } else {
    LOG(LS_ERROR) << "Invalid state for RTCP mux offer";
  }
  return ret;
}

bool RtcpMuxFilter::SetAnswer(bool answer_enable, ContentSource source) {
  bool ret = false;
  if ((state_ == ST_SENTOFFER && source == CS_REMOTE) ||
      (state_ == ST_RECEIVEDOFFER && source == CS_LOCAL)) {
    if (offer_enable_) {
      state_ = (answer_enable) ? ST_ACTIVE : ST_INIT;
      ret = true;
    } else {
      // If the offer didn't specify RTCP mux, the answer shouldn't either.
      if (!answer_enable) {
        ret = true;
        state_ = ST_INIT;
      } else {
        LOG(LS_WARNING) << "Invalid parameters in RTCP mux answer";
      }
    }
  } else {
    LOG(LS_ERROR) << "Invalid state for RTCP mux answer";
  }
  return ret;
}

bool RtcpMuxFilter::DemuxRtcp(const char* data, int len) {
  // If we're muxing RTP/RTCP, we must inspect each packet delivered and
  // determine whether it is RTP or RTCP. We do so by checking the packet type,
  // and assuming RTP if type is 0-63 or 96-127. For additional details, see
  // http://tools.ietf.org/html/rfc5761.
  // Note that if we offer RTCP mux, we may receive muxed RTCP before we
  // receive the answer, so we operate in that state too.
  if (!IsActive()) {
    return false;
  }

  int type = (len >= 2) ? (static_cast<uint8>(data[1]) & 0x7F) : 0;
  return (type >= 64 && type < 96);
}

}  // namespace cricket
