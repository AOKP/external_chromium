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

#include <string>
#include "talk/base/helpers.h"
#include "talk/base/logging.h"
#include "talk/base/thread.h"
#include "talk/session/phone/call.h"
#include "talk/session/phone/mediasessionclient.h"

namespace cricket {

const uint32 MSG_CHECKAUTODESTROY = 1;
const uint32 MSG_TERMINATECALL = 2;
const uint32 MSG_PLAYDTMF = 3;

namespace {
const int kDTMFDelay = 300;  // msec
const size_t kMaxDTMFDigits = 30;
const int kSendToVoicemailTimeout = 1000*20;
const int kNoVoicemailTimeout = 1000*180;
const int kMediaMonitorInterval = 1000*15;
}

Call::Call(MediaSessionClient* session_client)
    : id_(talk_base::CreateRandomId()),
      session_client_(session_client),
      local_renderer_(NULL),
      muted_(false),
      send_to_voicemail_(true),
      playing_dtmf_(false) {
}

Call::~Call() {
  while (sessions_.begin() != sessions_.end()) {
    Session *session = sessions_[0];
    RemoveSession(session);
    session_client_->session_manager()->DestroySession(session);
  }
  talk_base::Thread::Current()->Clear(this);
}

Session *Call::InitiateSession(const buzz::Jid &jid,
                               const CallOptions& options) {
  const SessionDescription* offer = session_client_->CreateOffer(options);

  Session *session = session_client_->CreateSession(this);
  AddSession(session, offer);
  session->Initiate(jid.Str(), offer);

  // After this timeout, terminate the call because the callee isn't
  // answering
  session_client_->session_manager()->signaling_thread()->Clear(this,
      MSG_TERMINATECALL);
  session_client_->session_manager()->signaling_thread()->PostDelayed(
    send_to_voicemail_ ? kSendToVoicemailTimeout : kNoVoicemailTimeout,
    this, MSG_TERMINATECALL);
  return session;
}

void Call::IncomingSession(
    Session* session, const SessionDescription* offer) {
  AddSession(session, offer);

  // Missed the first state, the initiate, which is needed by
  // call_client.
  SignalSessionState(this, session, Session::STATE_RECEIVEDINITIATE);
}

void Call::AcceptSession(BaseSession* session,
                         const cricket::CallOptions& options) {
  std::vector<Session *>::iterator it;
  it = std::find(sessions_.begin(), sessions_.end(), session);
  ASSERT(it != sessions_.end());
  if (it != sessions_.end()) {
    session->Accept(
        session_client_->CreateAnswer(session->remote_description(), options));
  }
}

void Call::RejectSession(BaseSession *session) {
  std::vector<Session *>::iterator it;
  it = std::find(sessions_.begin(), sessions_.end(), session);
  ASSERT(it != sessions_.end());
  // Assume polite decline.
  if (it != sessions_.end())
    session->Reject(STR_TERMINATE_DECLINE);
}

void Call::TerminateSession(BaseSession *session) {
  ASSERT(std::find(sessions_.begin(), sessions_.end(), session)
         != sessions_.end());
  std::vector<Session *>::iterator it;
  it = std::find(sessions_.begin(), sessions_.end(), session);
  // Assume polite terminations.
  if (it != sessions_.end())
    (*it)->Terminate();
}

void Call::Terminate() {
  // Copy the list so that we can iterate over it in a stable way
  std::vector<Session *> sessions = sessions_;

  // There may be more than one session to terminate
  std::vector<Session *>::iterator it;
  for (it = sessions.begin(); it != sessions.end(); it++)
    TerminateSession(*it);
}

void Call::SetLocalRenderer(VideoRenderer* renderer) {
  local_renderer_ = renderer;
  if (session_client_->GetFocus() == this) {
    session_client_->channel_manager()->SetLocalRenderer(renderer);
  }
}

void Call::SetVideoRenderer(BaseSession *session, uint32 ssrc,
                            VideoRenderer* renderer) {
  VideoChannel *video_channel = GetVideoChannel(session);
  if (video_channel) {
    video_channel->SetRenderer(ssrc, renderer);
  }
}

void Call::AddStream(BaseSession *session,
                     uint32 voice_ssrc, uint32 video_ssrc) {
  VoiceChannel *voice_channel = GetVoiceChannel(session);
  VideoChannel *video_channel = GetVideoChannel(session);
  if (voice_channel && voice_ssrc) {
    voice_channel->AddStream(voice_ssrc);
  }
  if (video_channel && video_ssrc) {
    video_channel->AddStream(video_ssrc, voice_ssrc);
  }
}

void Call::RemoveStream(BaseSession *session,
                        uint32 voice_ssrc, uint32 video_ssrc) {
  VoiceChannel *voice_channel = GetVoiceChannel(session);
  VideoChannel *video_channel = GetVideoChannel(session);
  if (voice_channel && voice_ssrc) {
    voice_channel->RemoveStream(voice_ssrc);
  }
  if (video_channel && video_ssrc) {
    video_channel->RemoveStream(video_ssrc);
  }
}

void Call::OnMessage(talk_base::Message *message) {
  switch (message->message_id) {
  case MSG_CHECKAUTODESTROY:
    // If no more sessions for this call, delete it
    if (sessions_.size() == 0)
      session_client_->DestroyCall(this);
    break;
  case MSG_TERMINATECALL:
    // Signal to the user that a timeout has happened and the call should
    // be sent to voicemail.
    if (send_to_voicemail_) {
      SignalSetupToCallVoicemail();
    }

    // Callee didn't answer - terminate call
    Terminate();
    break;
  case MSG_PLAYDTMF:
    ContinuePlayDTMF();
  }
}

const std::vector<Session *> &Call::sessions() {
  return sessions_;
}

bool Call::AddSession(Session *session, const SessionDescription* offer) {
  bool succeeded = true;
  VoiceChannel *voice_channel = NULL;
  VideoChannel *video_channel = NULL;

  const ContentInfo* audio_offer = GetFirstAudioContent(offer);
  const ContentInfo* video_offer = GetFirstVideoContent(offer);
  video_ = (video_offer != NULL);

  ASSERT(audio_offer != NULL);
  // Create voice channel and start a media monitor
  voice_channel = session_client_->channel_manager()->CreateVoiceChannel(
      session, audio_offer->name, video_);
  // voice_channel can be NULL in case of NullVoiceEngine.
  if (voice_channel) {
    voice_channel_map_[session->id()] = voice_channel;

    voice_channel->SignalMediaMonitor.connect(this, &Call::OnMediaMonitor);
    voice_channel->StartMediaMonitor(kMediaMonitorInterval);
  } else {
    succeeded = false;
  }

  // If desired, create video channel and start a media monitor
  if (video_ && succeeded) {
    video_channel = session_client_->channel_manager()->CreateVideoChannel(
        session, video_offer->name, true, voice_channel);
    // video_channel can be NULL in case of NullVideoEngine.
    if (video_channel) {
      video_channel_map_[session->id()] = video_channel;

      video_channel->SignalMediaMonitor.connect(this, &Call::OnMediaMonitor);
      video_channel->StartMediaMonitor(kMediaMonitorInterval);
    } else {
      succeeded = false;
    }
  }

  if (succeeded) {
    // Add session to list, create channels for this session
    sessions_.push_back(session);
    session->SignalState.connect(this, &Call::OnSessionState);
    session->SignalError.connect(this, &Call::OnSessionError);
    session->SignalReceivedTerminateReason
      .connect(this, &Call::OnReceivedTerminateReason);

    // If this call has the focus, enable this channel
    if (session_client_->GetFocus() == this) {
      voice_channel->Enable(true);
      if (video_channel) {
        video_channel->Enable(true);
      }
    }

    // Signal client
    SignalAddSession(this, session);
  }

  return succeeded;
}

void Call::RemoveSession(Session *session) {
  // Remove session from list
  std::vector<Session *>::iterator it_session;
  it_session = std::find(sessions_.begin(), sessions_.end(), session);
  if (it_session == sessions_.end())
    return;
  sessions_.erase(it_session);

  // Destroy video channel
  std::map<std::string, VideoChannel *>::iterator it_vchannel;
  it_vchannel = video_channel_map_.find(session->id());
  if (it_vchannel != video_channel_map_.end()) {
    VideoChannel *video_channel = it_vchannel->second;
    video_channel_map_.erase(it_vchannel);
    session_client_->channel_manager()->DestroyVideoChannel(video_channel);
  }

  // Destroy voice channel
  std::map<std::string, VoiceChannel *>::iterator it_channel;
  it_channel = voice_channel_map_.find(session->id());
  if (it_channel != voice_channel_map_.end()) {
    VoiceChannel *voice_channel = it_channel->second;
    voice_channel_map_.erase(it_channel);
    session_client_->channel_manager()->DestroyVoiceChannel(voice_channel);
  }

  // Signal client
  SignalRemoveSession(this, session);

  // The call auto destroys when the last session is removed
  talk_base::Thread::Current()->Post(this, MSG_CHECKAUTODESTROY);
}

VoiceChannel* Call::GetVoiceChannel(BaseSession* session) {
  std::map<std::string, VoiceChannel *>::iterator it
    = voice_channel_map_.find(session->id());
  return (it != voice_channel_map_.end()) ? it->second : NULL;
}

VideoChannel* Call::GetVideoChannel(BaseSession* session) {
  std::map<std::string, VideoChannel *>::iterator it
    = video_channel_map_.find(session->id());
  return (it != video_channel_map_.end()) ? it->second : NULL;
}

void Call::EnableChannels(bool enable) {
  std::vector<Session *>::iterator it;
  for (it = sessions_.begin(); it != sessions_.end(); it++) {
    VoiceChannel *voice_channel = GetVoiceChannel(*it);
    VideoChannel *video_channel = GetVideoChannel(*it);
    if (voice_channel != NULL)
      voice_channel->Enable(enable);
    if (video_channel != NULL)
      video_channel->Enable(enable);
  }
  session_client_->channel_manager()->SetLocalRenderer(
      (enable) ? local_renderer_ : NULL);
}

void Call::Mute(bool mute) {
  muted_ = mute;
  std::vector<Session *>::iterator it;
  for (it = sessions_.begin(); it != sessions_.end(); it++) {
    VoiceChannel *voice_channel = voice_channel_map_[(*it)->id()];
    if (voice_channel != NULL)
      voice_channel->Mute(mute);
  }
}

void Call::PressDTMF(int event) {
  // Queue up this digit
  if (queued_dtmf_.size() < kMaxDTMFDigits) {
    LOG(LS_INFO) << "Call::PressDTMF(" << event << ")";

    queued_dtmf_.push_back(event);

    if (!playing_dtmf_) {
      ContinuePlayDTMF();
    }
  }
}

void Call::ContinuePlayDTMF() {
  playing_dtmf_ = false;

  // Check to see if we have a queued tone
  if (queued_dtmf_.size() > 0) {
    playing_dtmf_ = true;

    int tone = queued_dtmf_.front();
    queued_dtmf_.pop_front();

    LOG(LS_INFO) << "Call::ContinuePlayDTMF(" << tone << ")";
    std::vector<Session *>::iterator it;
    for (it = sessions_.begin(); it != sessions_.end(); it++) {
      VoiceChannel *voice_channel = voice_channel_map_[(*it)->id()];
      if (voice_channel != NULL) {
        voice_channel->PressDTMF(tone, true);
      }
    }

    // Post a message to play the next tone or at least clear the playing_dtmf_
    // bit.
    talk_base::Thread::Current()->PostDelayed(kDTMFDelay, this, MSG_PLAYDTMF);
  }
}

void Call::Join(Call *call, bool enable) {
  while (call->sessions_.size() != 0) {
    // Move session
    Session *session = call->sessions_[0];
    call->sessions_.erase(call->sessions_.begin());
    sessions_.push_back(session);
    session->SignalState.connect(this, &Call::OnSessionState);
    session->SignalError.connect(this, &Call::OnSessionError);
    session->SignalReceivedTerminateReason
      .connect(this, &Call::OnReceivedTerminateReason);

    // Move voice channel
    std::map<std::string, VoiceChannel *>::iterator it_channel;
    it_channel = call->voice_channel_map_.find(session->id());
    if (it_channel != call->voice_channel_map_.end()) {
      VoiceChannel *voice_channel = (*it_channel).second;
      call->voice_channel_map_.erase(it_channel);
      voice_channel_map_[session->id()] = voice_channel;
      voice_channel->Enable(enable);
    }

    // Move video channel
    std::map<std::string, VideoChannel *>::iterator it_vchannel;
    it_vchannel = call->video_channel_map_.find(session->id());
    if (it_vchannel != call->video_channel_map_.end()) {
      VideoChannel *video_channel = (*it_vchannel).second;
      call->video_channel_map_.erase(it_vchannel);
      video_channel_map_[session->id()] = video_channel;
      video_channel->Enable(enable);
    }
  }
}

void Call::StartConnectionMonitor(BaseSession *session, int cms) {
  VoiceChannel *voice_channel = GetVoiceChannel(session);
  if (voice_channel) {
    voice_channel->SignalConnectionMonitor.connect(this,
        &Call::OnConnectionMonitor);
    voice_channel->StartConnectionMonitor(cms);
  }

  VideoChannel *video_channel = GetVideoChannel(session);
  if (video_channel) {
    video_channel->SignalConnectionMonitor.connect(this,
        &Call::OnConnectionMonitor);
    video_channel->StartConnectionMonitor(cms);
  }
}

void Call::StopConnectionMonitor(BaseSession *session) {
  VoiceChannel *voice_channel = GetVoiceChannel(session);
  if (voice_channel) {
    voice_channel->StopConnectionMonitor();
    voice_channel->SignalConnectionMonitor.disconnect(this);
  }

  VideoChannel *video_channel = GetVideoChannel(session);
  if (video_channel) {
    video_channel->StopConnectionMonitor();
    video_channel->SignalConnectionMonitor.disconnect(this);
  }
}

void Call::StartAudioMonitor(BaseSession *session, int cms) {
  VoiceChannel *voice_channel = GetVoiceChannel(session);
  if (voice_channel) {
    voice_channel->SignalAudioMonitor.connect(this, &Call::OnAudioMonitor);
    voice_channel->StartAudioMonitor(cms);
  }
}

void Call::StopAudioMonitor(BaseSession *session) {
  VoiceChannel *voice_channel = GetVoiceChannel(session);
  if (voice_channel) {
    voice_channel->StopAudioMonitor();
    voice_channel->SignalAudioMonitor.disconnect(this);
  }
}

void Call::OnConnectionMonitor(VoiceChannel *channel,
                               const std::vector<ConnectionInfo> &infos) {
  SignalConnectionMonitor(this, infos);
}

void Call::OnMediaMonitor(VoiceChannel *channel, const VoiceMediaInfo& info) {
  SignalMediaMonitor(this, info);
}

void Call::OnAudioMonitor(VoiceChannel *channel, const AudioInfo& info) {
  SignalAudioMonitor(this, info);
}

void Call::OnConnectionMonitor(VideoChannel *channel,
                               const std::vector<ConnectionInfo> &infos) {
  SignalVideoConnectionMonitor(this, infos);
}

void Call::OnMediaMonitor(VideoChannel *channel, const VideoMediaInfo& info) {
  SignalVideoMediaMonitor(this, info);
}

uint32 Call::id() {
  return id_;
}

void Call::OnSessionState(BaseSession *session, BaseSession::State state) {
  switch (state) {
    case Session::STATE_RECEIVEDACCEPT:
    case Session::STATE_RECEIVEDREJECT:
    case Session::STATE_RECEIVEDTERMINATE:
      session_client_->session_manager()->signaling_thread()->Clear(this,
          MSG_TERMINATECALL);
      break;
    default:
      break;
  }
  SignalSessionState(this, session, state);
}

void Call::OnSessionError(BaseSession *session, Session::Error error) {
  session_client_->session_manager()->signaling_thread()->Clear(this,
      MSG_TERMINATECALL);
  SignalSessionError(this, session, error);
}

void Call::OnReceivedTerminateReason(Session *session,
                                     const std::string &reason) {
  session_client_->session_manager()->signaling_thread()->Clear(this,
    MSG_TERMINATECALL);
  SignalReceivedTerminateReason(this, session, reason);
}

}  // namespace cricket
