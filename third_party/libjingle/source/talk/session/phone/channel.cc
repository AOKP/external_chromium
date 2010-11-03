/*
 * libjingle
 * Copyright 2004--2007, Google Inc.
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

#include "talk/session/phone/channel.h"
#include "talk/base/byteorder.h"
#include "talk/base/common.h"
#include "talk/base/logging.h"
#include "talk/p2p/base/transportchannel.h"
#include "talk/session/phone/channelmanager.h"
#include "talk/session/phone/mediasessionclient.h"

namespace cricket {

static const size_t kMaxPacketLen = 2048;

static const char* PacketType(bool rtcp) {
  return (!rtcp) ? "RTP" : "RTCP";
}

BaseChannel::BaseChannel(talk_base::Thread* thread, MediaEngine* media_engine,
                         MediaChannel* media_channel, BaseSession* session,
                         const std::string& content_name,
                         TransportChannel* transport_channel)
    : worker_thread_(thread), media_engine_(media_engine),
      session_(session), media_channel_(media_channel),
      content_name_(content_name),
      transport_channel_(transport_channel), rtcp_transport_channel_(NULL),
      enabled_(false), writable_(false), has_codec_(false), muted_(false) {
  ASSERT(worker_thread_ == talk_base::Thread::Current());
  media_channel_->SetInterface(this);
  transport_channel_->SignalWritableState.connect(
      this, &BaseChannel::OnWritableState);
  transport_channel_->SignalReadPacket.connect(
      this, &BaseChannel::OnChannelRead);

  LOG(LS_INFO) << "Created channel";

  session->SignalState.connect(this, &BaseChannel::OnSessionState);
}

BaseChannel::~BaseChannel() {
  ASSERT(worker_thread_ == talk_base::Thread::Current());
  StopConnectionMonitor();
  Clear();
  // We must destroy the media channel before the transport channel, otherwise
  // the media channel may try to send on the dead transport channel. NULLing
  // is not an effective strategy since the sends will come on another thread.
  delete media_channel_;
  set_rtcp_transport_channel(NULL);
  if (transport_channel_ != NULL)
    session_->DestroyChannel(content_name_, transport_channel_->name());
  LOG(LS_INFO) << "Destroyed channel";
}

bool BaseChannel::Enable(bool enable) {
  // Can be called from thread other than worker thread
  Send(enable ? MSG_ENABLE : MSG_DISABLE);
  return true;
}

bool BaseChannel::Mute(bool mute) {
  // Can be called from thread other than worker thread
  Send(mute ? MSG_MUTE : MSG_UNMUTE);
  return true;
}

bool BaseChannel::RemoveStream(uint32 ssrc) {
  StreamMessageData data(ssrc, 0);
  Send(MSG_REMOVESTREAM, &data);
  return true;
}

bool BaseChannel::SetRtcpCName(const std::string& cname) {
  SetRtcpCNameData data(cname);
  Send(MSG_SETRTCPCNAME, &data);
  return data.result;
}

bool BaseChannel::SetLocalContent(const MediaContentDescription* content,
                                  ContentAction action) {
  SetContentData data(content, action);
  Send(MSG_SETLOCALCONTENT, &data);
  return data.result;
}

bool BaseChannel::SetRemoteContent(const MediaContentDescription* content,
                                   ContentAction action) {
  SetContentData data(content, action);
  Send(MSG_SETREMOTECONTENT, &data);
  return data.result;
}

bool BaseChannel::SetMaxSendBandwidth(int max_bandwidth) {
  SetBandwidthData data(max_bandwidth);
  Send(MSG_SETMAXSENDBANDWIDTH, &data);
  return data.result;
}

void BaseChannel::StartConnectionMonitor(int cms) {
  socket_monitor_.reset(new SocketMonitor(transport_channel_,
                                          worker_thread(),
                                          talk_base::Thread::Current()));
  socket_monitor_->SignalUpdate.connect(
      this, &BaseChannel::OnConnectionMonitorUpdate);
  socket_monitor_->Start(cms);
}

void BaseChannel::StopConnectionMonitor() {
  if (socket_monitor_.get()) {
    socket_monitor_->Stop();
    socket_monitor_.reset();
  }
}

void BaseChannel::set_rtcp_transport_channel(TransportChannel* channel) {
  if (rtcp_transport_channel_ != channel) {
    if (rtcp_transport_channel_) {
      session_->DestroyChannel(content_name_, rtcp_transport_channel_->name());
    }
    rtcp_transport_channel_ = channel;
    if (rtcp_transport_channel_) {
      rtcp_transport_channel_->SignalWritableState.connect(
          this, &BaseChannel::OnWritableState);
      rtcp_transport_channel_->SignalReadPacket.connect(
          this, &BaseChannel::OnChannelRead);
    }
  }
}

int BaseChannel::SendPacket(const void *data, size_t len) {
  // SendPacket gets called from MediaEngine; send to socket
  // MediaEngine will call us on a random thread. The Send operation on the
  // socket is special in that it can handle this.
  // TODO: Actually, SendPacket cannot handle this. Need to fix ASAP.

  return SendPacket(false, data, len);
}

int BaseChannel::SendRtcp(const void *data, size_t len) {
  return SendPacket(true, data, len);
}

int BaseChannel::SetOption(SocketType type, talk_base::Socket::Option opt,
                           int value) {
  switch (type) {
    case ST_RTP: return transport_channel_->SetOption(opt, value);
    case ST_RTCP: return rtcp_transport_channel_->SetOption(opt, value);
    default: return -1;
  }
}

void BaseChannel::OnWritableState(TransportChannel* channel) {
  ASSERT(channel == transport_channel_ || channel == rtcp_transport_channel_);
  if (transport_channel_->writable()
      && (!rtcp_transport_channel_ || rtcp_transport_channel_->writable())) {
    ChannelWritable_w();
  } else {
    ChannelNotWritable_w();
  }
}

void BaseChannel::OnChannelRead(TransportChannel* channel,
                                const char* data, size_t len) {
  // OnChannelRead gets called from P2PSocket; now pass data to MediaEngine
  ASSERT(worker_thread_ == talk_base::Thread::Current());

  // When using RTCP multiplexing we might get RTCP packets on the RTP
  // transport. We feed RTP traffic into the demuxer to determine if it is RTCP.
  bool rtcp = (channel == rtcp_transport_channel_ ||
               rtcp_mux_filter_.DemuxRtcp(data, len));
  HandlePacket(rtcp, data, len);
}

int BaseChannel::SendPacket(bool rtcp, const void* data, size_t len) {
  // Protect ourselves against crazy data.
  if (len > kMaxPacketLen) {
    LOG(LS_ERROR) << "Dropping outgoing large "
                  << PacketType(rtcp) << " packet, size " << len;
    return -1;
  }

  // Make sure we have a place to send this packet before doing anything.
  // (We might get RTCP packets that we don't intend to send.)
  // If we've negotiated RTCP mux, send RTCP over the RTP transport.
  TransportChannel* channel = (!rtcp || rtcp_mux_filter_.IsActive()) ?
      transport_channel_ : rtcp_transport_channel_;
  if (!channel) {
    return -1;
  }

  // Protect if needed.
  uint8 work[kMaxPacketLen];
  const char* real_data = static_cast<const char*>(data);
  int real_len = len;
  if (srtp_filter_.IsActive()) {
    bool res;
    memcpy(work, data, len);
    if (!rtcp) {
      res = srtp_filter_.ProtectRtp(work, len, sizeof(work), &real_len);
    } else {
      res = srtp_filter_.ProtectRtcp(work, len, sizeof(work), &real_len);
    }
    if (!res) {
      LOG(LS_ERROR) << "Failed to protect "
                    << PacketType(rtcp) << " packet, size " << len;
      return -1;
    }

    real_data = reinterpret_cast<const char*>(work);
  }

  // Bon voyage. Return a number that the caller can understand.
  return (channel->SendPacket(real_data, real_len) == real_len) ? len : -1;
}

void BaseChannel::HandlePacket(bool rtcp, const char* data, size_t len) {
  // Protect ourselvs against crazy data.
  if (len > kMaxPacketLen) {
    LOG(LS_ERROR) << "Dropping incoming large "
                  << PacketType(rtcp) << " packet, size " << len;
    return;
  }

  // Unprotect the packet, if needed.
  uint8 work[kMaxPacketLen];
  const char* real_data = data;
  int real_len = len;
  if (srtp_filter_.IsActive()) {
    bool res;
    memcpy(work, data, len);
    if (!rtcp) {
      res = srtp_filter_.UnprotectRtp(work, len, &real_len);
    } else {
      res = srtp_filter_.UnprotectRtcp(work, len, &real_len);
    }
    if (!res) {
      LOG(LS_ERROR) << "Failed to unprotect "
                    << PacketType(rtcp) << " packet, size " << len;
      return;
    }
    real_data = reinterpret_cast<const char*>(work);
  }

  // Push it down to the media channel.
  if (!rtcp) {
    media_channel_->OnPacketReceived(real_data, real_len);
  } else {
    media_channel_->OnRtcpReceived(real_data, real_len);
  }
}

void BaseChannel::OnSessionState(BaseSession* session,
                                 BaseSession::State state) {
  // TODO: tear down the call via session->SetError() if the
  // SetXXXXDescription calls fail.
  const MediaContentDescription* content = NULL;
  switch (state) {
    case Session::STATE_SENTINITIATE:
      content = GetFirstContent(session->local_description());
      if (content) {
        SetLocalContent(content, CA_OFFER);
      }
      break;
    case Session::STATE_SENTACCEPT:
      content = GetFirstContent(session->local_description());
      if (content) {
        SetLocalContent(content, CA_ANSWER);
      }
      break;
    case Session::STATE_RECEIVEDINITIATE:
      content = GetFirstContent(session->remote_description());
      if (content) {
        SetRemoteContent(content, CA_OFFER);
      }
      break;
    case Session::STATE_RECEIVEDACCEPT:
      content = GetFirstContent(session->remote_description());
      if (content) {
        SetRemoteContent(content, CA_ANSWER);
      }
      break;
    default:
      break;
  }
}

void BaseChannel::EnableMedia_w() {
  ASSERT(worker_thread_ == talk_base::Thread::Current());
  if (enabled_)
    return;

  LOG(LS_INFO) << "Channel enabled";
  enabled_ = true;
  ChangeState();
}

void BaseChannel::DisableMedia_w() {
  ASSERT(worker_thread_ == talk_base::Thread::Current());
  if (!enabled_)
    return;

  LOG(LS_INFO) << "Channel disabled";
  enabled_ = false;
  ChangeState();
}

void BaseChannel::MuteMedia_w() {
  ASSERT(worker_thread_ == talk_base::Thread::Current());
  if (muted_)
    return;

  if (media_channel()->Mute(true)) {
    LOG(LS_INFO) << "Channel muted";
    muted_ = true;
  }
}

void BaseChannel::UnmuteMedia_w() {
  ASSERT(worker_thread_ == talk_base::Thread::Current());
  if (!muted_)
    return;

  if (media_channel()->Mute(false)) {
    LOG(LS_INFO) << "Channel unmuted";
    muted_ = false;
  }
}

void BaseChannel::ChannelWritable_w() {
  ASSERT(worker_thread_ == talk_base::Thread::Current());
  if (writable_)
    return;
  LOG(LS_INFO) << "Channel socket writable ("
               << transport_channel_->name().c_str() << ")";
  writable_ = true;
  ChangeState();
}

void BaseChannel::ChannelNotWritable_w() {
  ASSERT(worker_thread_ == talk_base::Thread::Current());
  if (!writable_)
    return;

  LOG(LS_INFO) << "Channel socket not writable ("
               << transport_channel_->name().c_str() << ")";
  writable_ = false;
  ChangeState();
}

bool BaseChannel::SetMaxSendBandwidth_w(int max_bandwidth) {
  return media_channel()->SetMaxSendBandwidth(max_bandwidth);
}

bool BaseChannel::SetRtcpCName_w(const std::string& cname) {
  return media_channel()->SetRtcpCName(cname);
}

bool BaseChannel::SetSrtp_w(const std::vector<CryptoParams>& cryptos,
                            ContentAction action, ContentSource src) {
  bool ret;
  if (action == CA_OFFER) {
    ret = srtp_filter_.SetOffer(cryptos, src);
  } else if (action == CA_ANSWER) {
    ret = srtp_filter_.SetAnswer(cryptos, src);
  } else {
    // CA_UPDATE, no crypto params.
    ret = true;
  }
  return ret;
}

bool BaseChannel::SetRtcpMux_w(bool enable, ContentAction action,
                               ContentSource src) {
  bool ret;
  if (action == CA_OFFER) {
    ret = rtcp_mux_filter_.SetOffer(enable, src);
  } else if (action == CA_ANSWER) {
    ret = rtcp_mux_filter_.SetAnswer(enable, src);
    if (ret && rtcp_mux_filter_.IsActive()) {
      // We activated RTCP mux, close down the RTCP transport.
      set_rtcp_transport_channel(NULL);
      // If the RTP transport is already writable, then so are we.
      if (transport_channel_->writable()) {
        ChannelWritable_w();
      }
    }
  } else {
    // CA_UPDATE, no RTCP mux info.
    ret = true;
  }
  return ret;
}

void BaseChannel::OnMessage(talk_base::Message *pmsg) {
  switch (pmsg->message_id) {
    case MSG_ENABLE:
      EnableMedia_w();
      break;
    case MSG_DISABLE:
      DisableMedia_w();
      break;

    case MSG_MUTE:
      MuteMedia_w();
      break;
    case MSG_UNMUTE:
      UnmuteMedia_w();
      break;

    case MSG_SETRTCPCNAME: {
      SetRtcpCNameData* data = static_cast<SetRtcpCNameData*>(pmsg->pdata);
      data->result = SetRtcpCName_w(data->cname);
      break;
    }

    case MSG_SETLOCALCONTENT: {
      SetContentData* data = static_cast<SetContentData*>(pmsg->pdata);
      data->result = SetLocalContent_w(data->content, data->action);
      break;
    }
    case MSG_SETREMOTECONTENT: {
      SetContentData* data = static_cast<SetContentData*>(pmsg->pdata);
      data->result = SetRemoteContent_w(data->content, data->action);
      break;
    }

    case MSG_REMOVESTREAM: {
      StreamMessageData* data = static_cast<StreamMessageData*>(pmsg->pdata);
      RemoveStream_w(data->ssrc1);
      break;
    }

    case MSG_SETMAXSENDBANDWIDTH: {
      SetBandwidthData* data = static_cast<SetBandwidthData*>(pmsg->pdata);
      data->result = SetMaxSendBandwidth_w(data->value);
      break;
    }
  }
}

void BaseChannel::Send(uint32 id, talk_base::MessageData *pdata) {
  worker_thread_->Send(this, id, pdata);
}

void BaseChannel::Post(uint32 id, talk_base::MessageData *pdata) {
  worker_thread_->Post(this, id, pdata);
}

void BaseChannel::PostDelayed(int cmsDelay, uint32 id,
                              talk_base::MessageData *pdata) {
  worker_thread_->PostDelayed(cmsDelay, this, id, pdata);
}

void BaseChannel::Clear(uint32 id, talk_base::MessageList* removed) {
  worker_thread_->Clear(this, id, removed);
}

VoiceChannel::VoiceChannel(talk_base::Thread* thread,
                           MediaEngine* media_engine,
                           VoiceMediaChannel* media_channel,
                           BaseSession* session,
                           const std::string& content_name,
                           bool rtcp)
    : BaseChannel(thread, media_engine, media_channel, session, content_name,
                  session->CreateChannel(content_name, "rtp")),
      received_media_(false) {
  if (rtcp) {
    set_rtcp_transport_channel(session->CreateChannel(content_name, "rtcp"));
  }
  // Can't go in BaseChannel because certain session states will
  // trigger pure virtual functions, such as GetFirstContent().
  OnSessionState(session, session->state());
}

VoiceChannel::~VoiceChannel() {
  StopAudioMonitor();
  StopMediaMonitor();
  // this can't be done in the base class, since it calls a virtual
  DisableMedia_w();
}

bool VoiceChannel::AddStream(uint32 ssrc) {
  StreamMessageData data(ssrc, 0);
  Send(MSG_ADDSTREAM, &data);
  return true;
}

bool VoiceChannel::SetRingbackTone(const void* buf, int len) {
  SetRingbackToneMessageData data(buf, len);
  Send(MSG_SETRINGBACKTONE, &data);
  return true;
}

// TODO: Handle early media the right way. We should get an explicit
// ringing message telling us to start playing local ringback, which we cancel
// if any early media actually arrives. For now, we do the opposite, which is
// to wait 1 second for early media, and start playing local ringback if none
// arrives.
void VoiceChannel::SetEarlyMedia(bool enable) {
  if (enable) {
    // Start the early media timeout
    PostDelayed(kEarlyMediaTimeout, MSG_EARLYMEDIATIMEOUT);
  } else {
    // Stop the timeout if currently going.
    Clear(MSG_EARLYMEDIATIMEOUT);
  }
}

bool VoiceChannel::PlayRingbackTone(bool play, bool loop) {
  PlayRingbackToneMessageData data(play, loop);
  Send(MSG_PLAYRINGBACKTONE, &data);
  return data.result;
}

bool VoiceChannel::PressDTMF(int digit, bool playout) {
  DtmfMessageData data(digit, playout);
  Send(MSG_PRESSDTMF, &data);
  return data.result;
}

void VoiceChannel::StartMediaMonitor(int cms) {
  media_monitor_.reset(new VoiceMediaMonitor(media_channel(), worker_thread(),
      talk_base::Thread::Current()));
  media_monitor_->SignalUpdate.connect(
      this, &VoiceChannel::OnMediaMonitorUpdate);
  media_monitor_->Start(cms);
}

void VoiceChannel::StopMediaMonitor() {
  if (media_monitor_.get()) {
    media_monitor_->Stop();
    media_monitor_->SignalUpdate.disconnect(this);
    media_monitor_.reset();
  }
}

void VoiceChannel::StartAudioMonitor(int cms) {
  audio_monitor_.reset(new AudioMonitor(this, talk_base::Thread::Current()));
  audio_monitor_
    ->SignalUpdate.connect(this, &VoiceChannel::OnAudioMonitorUpdate);
  audio_monitor_->Start(cms);
}

void VoiceChannel::StopAudioMonitor() {
  if (audio_monitor_.get()) {
    audio_monitor_->Stop();
    audio_monitor_.reset();
  }
}

int VoiceChannel::GetInputLevel_w() {
  return media_engine()->GetInputLevel();
}

int VoiceChannel::GetOutputLevel_w() {
  return media_channel()->GetOutputLevel();
}

void VoiceChannel::GetActiveStreams_w(AudioInfo::StreamList* actives) {
  media_channel()->GetActiveStreams(actives);
}

void VoiceChannel::OnChannelRead(TransportChannel* channel,
                                 const char* data, size_t len) {
  BaseChannel::OnChannelRead(channel, data, len);

  // Set a flag when we've received an RTP packet. If we're waiting for early
  // media, this will disable the timeout.
  // If we were playing out our local ringback, make sure it is stopped to
  // prevent it from interfering with the incoming media.
  if (!received_media_) {
    received_media_ = false;
    PlayRingbackTone_w(false, false);
  }
}

void VoiceChannel::ChangeState() {
  // render incoming data if we are the active call
  // we receive data on the default channel and multiplexed streams
  bool recv = enabled();
  media_channel()->SetPlayout(recv);

  // send outgoing data if we are the active call, have the
  // remote party's codec, and have a writable transport
  // we only send data on the default channel
  bool send = enabled() && has_codec() && writable();
  media_channel()->SetSend(send ? SEND_MICROPHONE : SEND_NOTHING);

  LOG(LS_INFO) << "Changing voice state, recv=" << recv << " send=" << send;
}

const MediaContentDescription* VoiceChannel::GetFirstContent(
    const SessionDescription* sdesc) {
  const ContentInfo* cinfo = GetFirstAudioContent(sdesc);
  if (cinfo == NULL)
    return NULL;

  return static_cast<const MediaContentDescription*>(cinfo->description);
}

bool VoiceChannel::SetLocalContent_w(const MediaContentDescription* content,
                                     ContentAction action) {
  ASSERT(worker_thread() == talk_base::Thread::Current());
  LOG(LS_INFO) << "Setting local voice description";

  const AudioContentDescription* audio =
      static_cast<const AudioContentDescription*>(content);
  ASSERT(audio != NULL);

  bool ret;
  // set SRTP
  ret = SetSrtp_w(audio->cryptos(), action, CS_LOCAL);
  // set RTCP mux
  if (ret) {
    ret = SetRtcpMux_w(audio->rtcp_mux(), action, CS_LOCAL);
  }
  // set payload type and config for voice codecs
  if (ret) {
    ret = media_channel()->SetRecvCodecs(audio->codecs());
  }
  return ret;
}

bool VoiceChannel::SetRemoteContent_w(const MediaContentDescription* content,
                                      ContentAction action) {
  ASSERT(worker_thread() == talk_base::Thread::Current());
  LOG(LS_INFO) << "Setting remote voice description";

  const AudioContentDescription* audio =
      static_cast<const AudioContentDescription*>(content);
  ASSERT(audio != NULL);

  bool ret;
  // set the sending SSRC, if the remote side gave us one
  if (audio->ssrc_set()) {
    media_channel()->SetSendSsrc(audio->ssrc());
  }
  // set SRTP
  ret = SetSrtp_w(audio->cryptos(), action, CS_REMOTE);
  // set RTCP mux
  if (ret) {
    ret = SetRtcpMux_w(audio->rtcp_mux(), action, CS_REMOTE);
  }
  // set codecs and payload types
  if (ret) {
    ret = media_channel()->SetSendCodecs(audio->codecs());
  }


  // update state
  if (ret) {
    set_has_codec(true);
    ChangeState();
  }
  return ret;
}

void VoiceChannel::AddStream_w(uint32 ssrc) {
  ASSERT(worker_thread() == talk_base::Thread::Current());
  media_channel()->AddStream(ssrc);
}

void VoiceChannel::RemoveStream_w(uint32 ssrc) {
  media_channel()->RemoveStream(ssrc);
}

void VoiceChannel::SetRingbackTone_w(const void* buf, int len) {
  ASSERT(worker_thread() == talk_base::Thread::Current());
  media_channel()->SetRingbackTone(static_cast<const char*>(buf), len);
}

bool VoiceChannel::PlayRingbackTone_w(bool play, bool loop) {
  ASSERT(worker_thread() == talk_base::Thread::Current());
  if (play) {
    LOG(LS_INFO) << "Playing ringback tone, loop=" << loop;
  } else {
    LOG(LS_INFO) << "Stopping ringback tone";
  }
  return media_channel()->PlayRingbackTone(play, loop);
}

void VoiceChannel::HandleEarlyMediaTimeout() {
  // This occurs on the main thread, not the worker thread.
  if (!received_media_) {
    LOG(LS_INFO) << "No early media received before timeout";
    SignalEarlyMediaTimeout(this);
  }
}

bool VoiceChannel::PressDTMF_w(int digit, bool playout) {
  if (!enabled() || !writable()) {
    return false;
  }

  return media_channel()->PressDTMF(digit, playout);
}

void VoiceChannel::OnMessage(talk_base::Message *pmsg) {
  switch (pmsg->message_id) {
    case MSG_ADDSTREAM: {
      StreamMessageData* data = static_cast<StreamMessageData*>(pmsg->pdata);
      AddStream_w(data->ssrc1);
      break;
    }
    case MSG_SETRINGBACKTONE: {
      SetRingbackToneMessageData* data =
          static_cast<SetRingbackToneMessageData*>(pmsg->pdata);
      SetRingbackTone_w(data->buf, data->len);
      break;
    }
    case MSG_PLAYRINGBACKTONE: {
      PlayRingbackToneMessageData* data =
          static_cast<PlayRingbackToneMessageData*>(pmsg->pdata);
      data->result = PlayRingbackTone_w(data->play, data->loop);
      break;
    }
    case MSG_EARLYMEDIATIMEOUT:
      HandleEarlyMediaTimeout();
      break;
    case MSG_PRESSDTMF: {
      DtmfMessageData* data = static_cast<DtmfMessageData*>(pmsg->pdata);
      data->result = PressDTMF_w(data->digit, data->playout);
      break;
    }

    default:
      BaseChannel::OnMessage(pmsg);
      break;
  }
}

void VoiceChannel::OnConnectionMonitorUpdate(
    SocketMonitor* monitor, const std::vector<ConnectionInfo>& infos) {
  SignalConnectionMonitor(this, infos);
}

void VoiceChannel::OnMediaMonitorUpdate(
    VoiceMediaChannel* media_channel, const VoiceMediaInfo& info) {
  ASSERT(media_channel == this->media_channel());
  SignalMediaMonitor(this, info);
}

void VoiceChannel::OnAudioMonitorUpdate(AudioMonitor* monitor,
                                        const AudioInfo& info) {
  SignalAudioMonitor(this, info);
}

VideoChannel::VideoChannel(talk_base::Thread* thread,
                           MediaEngine* media_engine,
                           VideoMediaChannel* media_channel,
                           BaseSession* session,
                           const std::string& content_name,
                           bool rtcp,
                           VoiceChannel* voice_channel)
    : BaseChannel(thread, media_engine, media_channel, session, content_name,
                  session->CreateChannel(content_name, "video_rtp")),
      voice_channel_(voice_channel), renderer_(NULL) {
  if (rtcp) {
    set_rtcp_transport_channel(
        session->CreateChannel(content_name, "video_rtcp"));
  }
  // Can't go in BaseChannel because certain session states will
  // trigger pure virtual functions, such as GetFirstContent()
  OnSessionState(session, session->state());
}

VideoChannel::~VideoChannel() {
  StopMediaMonitor();
  // this can't be done in the base class, since it calls a virtual
  DisableMedia_w();
}

bool VideoChannel::AddStream(uint32 ssrc, uint32 voice_ssrc) {
  StreamMessageData data(ssrc, voice_ssrc);
  Send(MSG_ADDSTREAM, &data);
  return true;
}

bool VideoChannel::SetRenderer(uint32 ssrc, VideoRenderer* renderer) {
  RenderMessageData data(ssrc, renderer);
  Send(MSG_SETRENDERER, &data);
  return true;
}


void VideoChannel::ChangeState() {
  // render incoming data if we are the active call
  // we receive data on the default channel and multiplexed streams
  bool recv = enabled();
  media_channel()->SetRender(recv);

  // send outgoing data if we are the active call, have the
  // remote party's codec, and have a writable transport
  // we only send data on the default channel
  bool send = enabled() && has_codec() && writable();
  media_channel()->SetSend(send);

  LOG(LS_INFO) << "Changing video state, recv=" << recv << " send=" << send;
}

void VideoChannel::StartMediaMonitor(int cms) {
  media_monitor_.reset(new VideoMediaMonitor(media_channel(), worker_thread(),
      talk_base::Thread::Current()));
  media_monitor_->SignalUpdate.connect(
      this, &VideoChannel::OnMediaMonitorUpdate);
  media_monitor_->Start(cms);
}

void VideoChannel::StopMediaMonitor() {
  if (media_monitor_.get()) {
    media_monitor_->Stop();
    media_monitor_.reset();
  }
}

const MediaContentDescription* VideoChannel::GetFirstContent(
    const SessionDescription* sdesc) {
  const ContentInfo* cinfo = GetFirstVideoContent(sdesc);
  if (cinfo == NULL)
    return NULL;

  return static_cast<const MediaContentDescription*>(cinfo->description);
}

bool VideoChannel::SetLocalContent_w(const MediaContentDescription* content,
                                     ContentAction action) {
  ASSERT(worker_thread() == talk_base::Thread::Current());
  LOG(LS_INFO) << "Setting local video description";

  const VideoContentDescription* video =
      static_cast<const VideoContentDescription*>(content);
  ASSERT(video != NULL);

  bool ret;
  // set SRTP
  ret = SetSrtp_w(video->cryptos(), action, CS_LOCAL);
  // set RTCP mux
  if (ret) {
    ret = SetRtcpMux_w(video->rtcp_mux(), action, CS_LOCAL);
  }
  // set payload types and config for receiving video
  if (ret) {
    ret = media_channel()->SetRecvCodecs(video->codecs());
  }
  return ret;
}

bool VideoChannel::SetRemoteContent_w(const MediaContentDescription* content,
                                      ContentAction action) {
  ASSERT(worker_thread() == talk_base::Thread::Current());
  LOG(LS_INFO) << "Setting remote video description";

  const VideoContentDescription* video =
      static_cast<const VideoContentDescription*>(content);
  ASSERT(video != NULL);

  bool ret;
  // set the sending SSRC, if the remote side gave us one
  // TODO: remove this, since it's not needed.
  if (video->ssrc_set()) {
    media_channel()->SetSendSsrc(video->ssrc());
  }
  // set SRTP
  ret = SetSrtp_w(video->cryptos(), action, CS_REMOTE);
  // set RTCP mux
  if (ret) {
    ret = SetRtcpMux_w(video->rtcp_mux(), action, CS_REMOTE);
  }
  // TODO: Set bandwidth appropriately here.
  if (ret) {
    ret = media_channel()->SetSendCodecs(video->codecs());
  }
  media_channel()->SetRtpExtensionHeaders(!video->rtp_headers_disabled());
  if (ret) {
    set_has_codec(true);
    ChangeState();
  }
  return ret;
}

void VideoChannel::AddStream_w(uint32 ssrc, uint32 voice_ssrc) {
  media_channel()->AddStream(ssrc, voice_ssrc);
}

void VideoChannel::RemoveStream_w(uint32 ssrc) {
  media_channel()->RemoveStream(ssrc);
}

void VideoChannel::SetRenderer_w(uint32 ssrc, VideoRenderer* renderer) {
  media_channel()->SetRenderer(ssrc, renderer);
}


void VideoChannel::OnMessage(talk_base::Message *pmsg) {
  switch (pmsg->message_id) {
    case MSG_ADDSTREAM: {
      StreamMessageData* data = static_cast<StreamMessageData*>(pmsg->pdata);
      AddStream_w(data->ssrc1, data->ssrc2);
      break;
    }
    case MSG_SETRENDERER: {
      RenderMessageData* data = static_cast<RenderMessageData*>(pmsg->pdata);
      SetRenderer_w(data->ssrc, data->renderer);
      break;
    }
  default:
    BaseChannel::OnMessage(pmsg);
    break;
  }
}

void VideoChannel::OnConnectionMonitorUpdate(
    SocketMonitor *monitor, const std::vector<ConnectionInfo> &infos) {
  SignalConnectionMonitor(this, infos);
}

void VideoChannel::OnMediaMonitorUpdate(
    VideoMediaChannel* media_channel, const VideoMediaInfo &info) {
  ASSERT(media_channel == this->media_channel());
  SignalMediaMonitor(this, info);
}

// TODO: Move to own file in a future CL.
// Leaving here for now to avoid having to mess with the Mac build.
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
  // http://tools.ietf.org/html/draft-ietf-avt-rtp-and-rtcp-mux-07.
  // Note that if we offer RTCP mux, we may receive muxed RTCP before we
  // receive the answer, so we operate in that state too.
  if (!IsActive()) {
    return false;
  }

  int type = (len >= 2) ? (static_cast<uint8>(data[1]) & 0x7F) : 0;
  return (type >= 64 && type < 96);
}

}  // namespace cricket
