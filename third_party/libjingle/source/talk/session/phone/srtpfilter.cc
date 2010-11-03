/*
 * libjingle
 * Copyright 2009, Google Inc.
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

#undef HAVE_CONFIG_H  // talk's config.h conflicts with the one included by the
                      // libsrtp headers.  Don't use it.
#include "talk/session/phone/srtpfilter.h"

#include <algorithm>
#include <cstring>

#include "talk/base/base64.h"
#include "talk/base/logging.h"

// TODO: For the XCode build, we force SRTP (b/2500074)
#if defined(OSX) && !defined(HAVE_SRTP)
#define HAVE_SRTP 1
#endif

// Enable this line to turn on SRTP debugging
// #define SRTP_DEBUG

#ifdef HAVE_SRTP
#ifdef SRTP_RELATIVE_PATH
#include "srtp.h"  // NOLINT
#else
#include "third_party/libsrtp/include/srtp.h"
#endif  // SRTP_RELATIVE_PATH
#ifdef _DEBUG
extern "C" debug_module_t mod_srtp;
#endif
#else
// SrtpFilter needs that constant.
#define SRTP_MASTER_KEY_LEN 30
#endif  // HAVE_SRTP

namespace cricket {

const std::string& CS_DEFAULT = CS_AES_CM_128_HMAC_SHA1_80;
const std::string CS_AES_CM_128_HMAC_SHA1_80 = "AES_CM_128_HMAC_SHA1_80";
const std::string CS_AES_CM_128_HMAC_SHA1_32 = "AES_CM_128_HMAC_SHA1_32";

SrtpFilter::SrtpFilter() : state_(ST_INIT) {
}

SrtpFilter::~SrtpFilter() {
}

bool SrtpFilter::IsActive() const {
  return (state_ == ST_ACTIVE);
}

bool SrtpFilter::SetOffer(const std::vector<CryptoParams>& offer_params,
                          ContentSource source) {
  bool ret = false;
  if (state_ == ST_INIT) {
    ret = StoreParams(offer_params, source);
  } else {
    LOG(LS_ERROR) << "Invalid state for SRTP offer";
  }
  return ret;
}

bool SrtpFilter::SetAnswer(const std::vector<CryptoParams>& answer_params,
                           ContentSource source) {
  bool ret = false;
  if ((state_ == ST_SENTOFFER && source == CS_REMOTE) ||
      (state_ == ST_RECEIVEDOFFER && source == CS_LOCAL)) {
    // If the answer requests crypto, finalize the parameters and apply them.
    // Otherwise, complete the negotiation of a unencrypted session.
    if (!answer_params.empty()) {
      CryptoParams selected_params;
      ret = NegotiateParams(answer_params, &selected_params);
      if (ret) {
        if (state_ == ST_SENTOFFER) {
          ret = ApplyParams(selected_params, answer_params[0]);
        } else {  // ST_RECEIVEDOFFER
          ret = ApplyParams(answer_params[0], selected_params);
        }
      }
    } else {
      ret = ResetParams();
    }
  } else {
    LOG(LS_ERROR) << "Invalid state for SRTP answer";
  }
  return ret;
}

bool SrtpFilter::ProtectRtp(void* p, int in_len, int max_len, int* out_len) {
  if (!IsActive()) {
    return false;
  }
  return send_session_.ProtectRtp(p, in_len, max_len, out_len);
}

bool SrtpFilter::ProtectRtcp(void* p, int in_len, int max_len, int* out_len) {
  if (!IsActive()) {
    return false;
  }
  return send_session_.ProtectRtcp(p, in_len, max_len, out_len);
}

bool SrtpFilter::UnprotectRtp(void* p, int in_len, int* out_len) {
  if (!IsActive()) {
    return false;
  }
  return recv_session_.UnprotectRtp(p, in_len, out_len);
}

bool SrtpFilter::UnprotectRtcp(void* p, int in_len, int* out_len) {
  if (!IsActive()) {
    return false;
  }
  return recv_session_.UnprotectRtcp(p, in_len, out_len);
}


bool SrtpFilter::StoreParams(const std::vector<CryptoParams>& params,
                             ContentSource source) {
  offer_params_ = params;
  state_ = (source == CS_LOCAL) ? ST_SENTOFFER : ST_RECEIVEDOFFER;
  return true;
}

bool SrtpFilter::NegotiateParams(const std::vector<CryptoParams>& answer_params,
                                 CryptoParams* selected_params) {
  // We're processing an accept. We should have exactly one set of params,
  // unless the offer didn't mention crypto, in which case we shouldn't be here.
  bool ret = (answer_params.size() == 1U && !offer_params_.empty());
  if (ret) {
    // We should find a match between the answer params and the offered params.
    std::vector<CryptoParams>::const_iterator it;
    for (it = offer_params_.begin(); it != offer_params_.end(); ++it) {
      if (answer_params[0].Matches(*it)) {
        break;
      }
    }

    if (it != offer_params_.end()) {
      *selected_params = *it;
    } else {
      ret = false;
    }
  }

  if (!ret) {
    LOG(LS_WARNING) << "Invalid parameters in SRTP answer";
  }
  return ret;
}

bool SrtpFilter::ApplyParams(const CryptoParams& send_params,
                             const CryptoParams& recv_params) {
  // TODO: Zero these buffers after use.
  bool ret;
  uint8 send_key[SRTP_MASTER_KEY_LEN], recv_key[SRTP_MASTER_KEY_LEN];
  ret = (ParseKeyParams(send_params.key_params, send_key, sizeof(send_key)) &&
         ParseKeyParams(recv_params.key_params, recv_key, sizeof(recv_key)));
  if (ret) {
    ret = (send_session_.SetSend(send_params.cipher_suite,
                                 send_key, sizeof(send_key)) &&
           recv_session_.SetRecv(recv_params.cipher_suite,
                                 recv_key, sizeof(recv_key)));
  }
  if (ret) {
    offer_params_.clear();
    state_ = ST_ACTIVE;
  } else {
    LOG(LS_WARNING) << "Failed to apply negotiated SRTP parameters";
  }
  return ret;
}

bool SrtpFilter::ResetParams() {
  offer_params_.clear();
  state_ = ST_INIT;
  return true;
}

bool SrtpFilter::ParseKeyParams(const std::string& key_params,
                                uint8* key, int len) {
  // example key_params: "inline:YUJDZGVmZ2hpSktMbW9QUXJzVHVWd3l6MTIzNDU2"

  // Fail if key-method is wrong.
  if (key_params.find("inline:") != 0) {
    return false;
  }

  // Fail if base64 decode fails, or the key is the wrong size.
  std::string key_b64(key_params.substr(7)), key_str;
  if (!talk_base::Base64::Decode(key_b64, talk_base::Base64::DO_STRICT,
                                 &key_str, NULL) ||
      static_cast<int>(key_str.size()) != len) {
    return false;
  }

  memcpy(key, key_str.c_str(), len);
  return true;
}

///////////////////////////////////////////////////////////////////////////////
// SrtpSession

#ifdef HAVE_SRTP

bool SrtpSession::inited_ = false;
std::list<SrtpSession*> SrtpSession::sessions_;

SrtpSession::SrtpSession()
    : session_(NULL), rtp_auth_tag_len_(0), rtcp_auth_tag_len_(0) {
  sessions_.push_back(this);
}

SrtpSession::~SrtpSession() {
  sessions_.erase(std::find(sessions_.begin(), sessions_.end(), this));
  if (session_) {
    srtp_dealloc(session_);
  }
}

bool SrtpSession::SetSend(const std::string& cs, const uint8* key, int len) {
  return SetKey(ssrc_any_outbound, cs, key, len);
}

bool SrtpSession::SetRecv(const std::string& cs, const uint8* key, int len) {
  return SetKey(ssrc_any_inbound, cs, key, len);
}

bool SrtpSession::ProtectRtp(void* p, int in_len, int max_len, int* out_len) {
  if (!session_)
    return false;
  int need_len = in_len + rtp_auth_tag_len_;  // NOLINT
  if (max_len < need_len)
    return false;
  *out_len = in_len;
  int err = srtp_protect(session_, p, out_len);
  if (err != err_status_ok) {
    LOG(LS_WARNING) << "Failed to protect SRTP packet, err=" << err;
    return false;
  }
  return true;
}

bool SrtpSession::ProtectRtcp(void* p, int in_len, int max_len, int* out_len) {
  if (!session_)
    return false;
  int need_len = in_len + sizeof(uint32) + rtcp_auth_tag_len_;  // NOLINT
  if (max_len < need_len)
    return false;
  *out_len = in_len;
  int err = srtp_protect_rtcp(session_, p, out_len);
  if (err != err_status_ok) {
    LOG(LS_WARNING) << "Failed to protect SRTCP packet, err=" << err;
    return false;
  }
  return true;
}

bool SrtpSession::UnprotectRtp(void* p, int in_len, int* out_len) {
  if (!session_)
    return false;
  *out_len = in_len;
  int err = srtp_unprotect(session_, p, out_len);
  if (err != err_status_ok) {
    LOG(LS_WARNING) << "Failed to unprotect SRTP packet, err=" << err;
    return false;
  }
  return true;
}

bool SrtpSession::UnprotectRtcp(void* p, int in_len, int* out_len) {
  if (!session_)
    return false;
  *out_len = in_len;
  int err = srtp_unprotect_rtcp(session_, p, out_len);
  if (err != err_status_ok) {
    LOG(LS_WARNING) << "Failed to unprotect SRTCP packet, err=" << err;
    return false;
  }
  return true;
}

bool SrtpSession::SetKey(int type, const std::string& cs,
                         const uint8* key, int len) {
  if (session_) {
    return false;
  }

  if (!Init()) {
    return false;
  }

  srtp_policy_t policy;
  memset(&policy, 0, sizeof(policy));

  if (cs == CS_AES_CM_128_HMAC_SHA1_80) {
    crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtp);
    crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtcp);
  } else if (cs == CS_AES_CM_128_HMAC_SHA1_32) {
    crypto_policy_set_aes_cm_128_hmac_sha1_32(&policy.rtp);   // rtp is 32,
    crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtcp);  // rtcp still 80
  } else {
    return false;
  }

  if (!key || len != SRTP_MASTER_KEY_LEN) {
    return false;
  }

  policy.ssrc.type = static_cast<ssrc_type_t>(type);
  policy.ssrc.value = 0;
  policy.key = const_cast<uint8*>(key);
  // TODO parse window size from WSH session-param
  policy.window_size = 1024;
  policy.allow_repeat_tx = 1;
  policy.next = NULL;

  int err = srtp_create(&session_, &policy);
  if (err != err_status_ok) {
    LOG(LS_ERROR) << "Failed to create SRTP session, err=" << err;
    return false;
  }

  rtp_auth_tag_len_ = policy.rtp.auth_tag_len;
  rtcp_auth_tag_len_ = policy.rtcp.auth_tag_len;
  return true;
}

bool SrtpSession::Init() {
  if (!inited_) {
    int err;
#ifdef DEBUG_SRTP
    debug_on(mod_srtp);
#endif
    err = srtp_init();
    if (err != err_status_ok) {
      LOG(LS_ERROR) << "Failed to init SRTP, err=" << err;
      return false;
    }

    err = srtp_install_event_handler(&SrtpSession::HandleEventThunk);
    if (err != err_status_ok) {
      LOG(LS_ERROR) << "Failed to install SRTP event handler, err=" << err;
      return false;
    }

    inited_ = true;
  }

  return true;
}

void SrtpSession::HandleEvent(const srtp_event_data_t* ev) {
  // TODO: Do something about events.
}

void SrtpSession::HandleEventThunk(srtp_event_data_t* ev) {
  for (std::list<SrtpSession*>::iterator it = sessions_.begin();
       it != sessions_.end(); ++it) {
    if ((*it)->session_ == ev->session) {
      (*it)->HandleEvent(ev);
      break;
    }
  }
}

#else   // !HAVE_SRTP

namespace {
bool SrtpNotAvailable(const char *func) {
  LOG(LS_ERROR) << func << ": SRTP is not available on your system.";
  return false;
}
}  // anonymous namespace

SrtpSession::SrtpSession() {
  LOG(WARNING) << "SRTP implementation is missing.";
}

SrtpSession::~SrtpSession() {
}

bool SrtpSession::SetSend(const std::string& cs, const uint8* key, int len) {
  return SrtpNotAvailable(__FUNCTION__);
}

bool SrtpSession::SetRecv(const std::string& cs, const uint8* key, int len) {
  return SrtpNotAvailable(__FUNCTION__);
}

bool SrtpSession::ProtectRtp(void* data, int in_len, int max_len,
                             int* out_len) {
  return SrtpNotAvailable(__FUNCTION__);
}

bool SrtpSession::ProtectRtcp(void* data, int in_len, int max_len,
                              int* out_len) {
  return SrtpNotAvailable(__FUNCTION__);
}

bool SrtpSession::UnprotectRtp(void* data, int in_len, int* out_len) {
  return SrtpNotAvailable(__FUNCTION__);
}

bool SrtpSession::UnprotectRtcp(void* data, int in_len, int* out_len) {
  return SrtpNotAvailable(__FUNCTION__);
}

#endif  // HAVE_SRTP
}  // namespace cricket
