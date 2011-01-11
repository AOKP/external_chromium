// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/speech/speech_recognizer.h"

#include "base/ref_counted.h"
#include "base/scoped_ptr.h"
#include "base/time.h"
#include "chrome/browser/browser_thread.h"
#include "chrome/browser/profile.h"
#include "chrome/common/net/url_request_context_getter.h"
#include "third_party/speex/include/speex/speex.h"

using media::AudioInputController;
using std::list;
using std::string;

namespace {
const char* const kContentTypeSpeex =
    "audio/x-speex-with-header-byte; rate=16000";
const int kSpeexEncodingQuality = 8;
const int kMaxSpeexFrameLength = 110;  // (44kbps rate sampled at 32kHz).

// Since the frame length gets written out as a byte in the encoded packet,
// make sure it is within the byte range.
COMPILE_ASSERT(kMaxSpeexFrameLength <= 0xFF, invalidLength);

// The following constants are related to the volume level indicator shown in
// the UI for recorded audio.
// Multiplier used when new volume is greater than previous level.
const float kUpSmoothingFactor = 0.9f;
// Multiplier used when new volume is lesser than previous level.
const float kDownSmoothingFactor = 0.4f;
const float kAudioMeterMinDb = 10.0f;  // Lower bar for volume meter.
const float kAudioMeterDbRange = 25.0f;
}  // namespace

namespace speech_input {

const int SpeechRecognizer::kAudioSampleRate = 16000;
const int SpeechRecognizer::kAudioPacketIntervalMs = 100;
const int SpeechRecognizer::kNumAudioChannels = 1;
const int SpeechRecognizer::kNumBitsPerAudioSample = 16;
const int SpeechRecognizer::kNoSpeechTimeoutSec = 8;
const int SpeechRecognizer::kEndpointerEstimationTimeMs = 300;

// Provides a simple interface to encode raw audio using the Speex codec.
class SpeexEncoder {
 public:
  SpeexEncoder();
  ~SpeexEncoder();

  int samples_per_frame() const { return samples_per_frame_; }

  // Encodes each frame of raw audio in |samples| and adds the
  // encoded frames as a set of strings to the |encoded_frames| list.
  // Ownership of the newly added strings is transferred to the caller.
  void Encode(const short* samples,
              int num_samples,
              std::list<std::string*>* encoded_frames);

 private:
  SpeexBits bits_;
  void* encoder_state_;
  int samples_per_frame_;
  char encoded_frame_data_[kMaxSpeexFrameLength + 1];  // +1 for the frame size.
};

SpeexEncoder::SpeexEncoder() {
  // speex_bits_init() does not initialize all of the |bits_| struct.
  memset(&bits_, 0, sizeof(bits_));
  speex_bits_init(&bits_);
  encoder_state_ = speex_encoder_init(&speex_wb_mode);
  DCHECK(encoder_state_);
  speex_encoder_ctl(encoder_state_, SPEEX_GET_FRAME_SIZE, &samples_per_frame_);
  DCHECK(samples_per_frame_ > 0);
  int quality = kSpeexEncodingQuality;
  speex_encoder_ctl(encoder_state_, SPEEX_SET_QUALITY, &quality);
  int vbr = 1;
  speex_encoder_ctl(encoder_state_, SPEEX_SET_VBR, &vbr);
  memset(encoded_frame_data_, 0, sizeof(encoded_frame_data_));
}

SpeexEncoder::~SpeexEncoder() {
  speex_bits_destroy(&bits_);
  speex_encoder_destroy(encoder_state_);
}

void SpeexEncoder::Encode(const short* samples,
                          int num_samples,
                          std::list<std::string*>* encoded_frames) {
  // Drop incomplete frames, typically those which come in when recording stops.
  num_samples -= (num_samples % samples_per_frame_);
  for (int i = 0; i < num_samples; i += samples_per_frame_) {
    speex_bits_reset(&bits_);
    speex_encode_int(encoder_state_, const_cast<spx_int16_t*>(samples + i),
                     &bits_);

    // Encode the frame and place the size of the frame as the first byte. This
    // is the packet format for MIME type x-speex-with-header-byte.
    int frame_length = speex_bits_write(&bits_, encoded_frame_data_ + 1,
                                        kMaxSpeexFrameLength);
    encoded_frame_data_[0] = static_cast<char>(frame_length);
    encoded_frames->push_back(new string(encoded_frame_data_,
                                         frame_length + 1));
  }
}

SpeechRecognizer::SpeechRecognizer(Delegate* delegate,
                                   int caller_id,
                                   const std::string& language,
                                   const std::string& grammar,
                                   const std::string& hardware_info)
    : delegate_(delegate),
      caller_id_(caller_id),
      language_(language),
      grammar_(grammar),
      hardware_info_(hardware_info),
      encoder_(new SpeexEncoder()),
      endpointer_(kAudioSampleRate),
      num_samples_recorded_(0),
      audio_level_(0.0f) {
  endpointer_.set_speech_input_complete_silence_length(
      base::Time::kMicrosecondsPerSecond / 2);
  endpointer_.set_long_speech_input_complete_silence_length(
      base::Time::kMicrosecondsPerSecond);
  endpointer_.set_long_speech_length(3 * base::Time::kMicrosecondsPerSecond);
  endpointer_.StartSession();
}

SpeechRecognizer::~SpeechRecognizer() {
  // Recording should have stopped earlier due to the endpointer or
  // |StopRecording| being called.
  DCHECK(!audio_controller_.get());
  DCHECK(!request_.get() || !request_->HasPendingRequest());
  DCHECK(audio_buffers_.empty());
  endpointer_.EndSession();
}

bool SpeechRecognizer::StartRecording() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(!audio_controller_.get());
  DCHECK(!request_.get() || !request_->HasPendingRequest());

  // The endpointer needs to estimate the environment/background noise before
  // starting to treat the audio as user input. In |HandleOnData| we wait until
  // such time has passed before switching to user input mode.
  endpointer_.SetEnvironmentEstimationMode();

  int samples_per_packet = (kAudioSampleRate * kAudioPacketIntervalMs) / 1000;
  DCHECK((samples_per_packet % encoder_->samples_per_frame()) == 0);
  AudioParameters params(AudioParameters::AUDIO_PCM_LINEAR, kNumAudioChannels,
                         kAudioSampleRate, kNumBitsPerAudioSample,
                         samples_per_packet);
  audio_controller_ = AudioInputController::Create(this, params);
  DCHECK(audio_controller_.get());
  VLOG(1) << "SpeechRecognizer starting record.";
  num_samples_recorded_ = 0;
  audio_controller_->Record();

  return true;
}

void SpeechRecognizer::CancelRecognition() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  DCHECK(audio_controller_.get() || request_.get());

  // Stop recording if required.
  if (audio_controller_.get()) {
    VLOG(1) << "SpeechRecognizer stopping record.";
    audio_controller_->Close();
    audio_controller_ = NULL;  // Releases the ref ptr.
  }

  VLOG(1) << "SpeechRecognizer canceling recognition.";
  ReleaseAudioBuffers();
  request_.reset();
}

void SpeechRecognizer::StopRecording() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));

  // If audio recording has already stopped and we are in recognition phase,
  // silently ignore any more calls to stop recording.
  if (!audio_controller_.get())
    return;

  VLOG(1) << "SpeechRecognizer stopping record.";
  audio_controller_->Close();
  audio_controller_ = NULL;  // Releases the ref ptr.

  delegate_->DidCompleteRecording(caller_id_);

  // If we haven't got any audio yet end the recognition sequence here.
  if (audio_buffers_.empty()) {
    // Guard against the delegate freeing us until we finish our job.
    scoped_refptr<SpeechRecognizer> me(this);
    delegate_->DidCompleteRecognition(caller_id_);
    return;
  }

  // We now have recorded audio in our buffers, so start a recognition request.
  // Since the http request takes a single string as POST data, allocate
  // one and copy over bytes from the audio buffers to the string.
  int audio_buffer_length = 0;
  for (AudioBufferQueue::iterator it = audio_buffers_.begin();
       it != audio_buffers_.end(); it++) {
    audio_buffer_length += (*it)->length();
  }
  string data;
  data.reserve(audio_buffer_length);
  for (AudioBufferQueue::iterator it = audio_buffers_.begin();
       it != audio_buffers_.end(); it++) {
    data.append(*(*it));
  }

  DCHECK(!request_.get());
  request_.reset(new SpeechRecognitionRequest(
      Profile::GetDefaultRequestContext(), this));
  request_->Send(language_, grammar_, hardware_info_, kContentTypeSpeex, data);
  ReleaseAudioBuffers();  // No need to keep the audio anymore.
}

void SpeechRecognizer::ReleaseAudioBuffers() {
  for (AudioBufferQueue::iterator it = audio_buffers_.begin();
       it != audio_buffers_.end(); it++)
    delete *it;
  audio_buffers_.clear();
}

// Invoked in the audio thread.
void SpeechRecognizer::OnError(AudioInputController* controller,
                               int error_code) {
  BrowserThread::PostTask(BrowserThread::IO, FROM_HERE,
                         NewRunnableMethod(this,
                                           &SpeechRecognizer::HandleOnError,
                                           error_code));
}

void SpeechRecognizer::HandleOnError(int error_code) {
  LOG(WARNING) << "SpeechRecognizer::HandleOnError, code=" << error_code;

  // Check if we are still recording before canceling recognition, as
  // recording might have been stopped after this error was posted to the queue
  // by |OnError|.
  if (!audio_controller_.get())
    return;

  InformErrorAndCancelRecognition(RECOGNIZER_ERROR_CAPTURE);
}

void SpeechRecognizer::OnData(AudioInputController* controller,
                              const uint8* data, uint32 size) {
  if (size == 0)  // This could happen when recording stops and is normal.
    return;

  string* str_data = new string(reinterpret_cast<const char*>(data), size);
  BrowserThread::PostTask(BrowserThread::IO, FROM_HERE,
                         NewRunnableMethod(this,
                                           &SpeechRecognizer::HandleOnData,
                                           str_data));
}

void SpeechRecognizer::HandleOnData(string* data) {
  // Check if we are still recording and if not discard this buffer, as
  // recording might have been stopped after this buffer was posted to the queue
  // by |OnData|.
  if (!audio_controller_.get()) {
    delete data;
    return;
  }

  const short* samples = reinterpret_cast<const short*>(data->data());
  DCHECK((data->length() % sizeof(short)) == 0);
  int num_samples = data->length() / sizeof(short);

  encoder_->Encode(samples, num_samples, &audio_buffers_);
  float rms;
  endpointer_.ProcessAudio(samples, num_samples, &rms);
  delete data;
  num_samples_recorded_ += num_samples;

  if (endpointer_.IsEstimatingEnvironment()) {
    // Check if we have gathered enough audio for the endpointer to do
    // environment estimation and should move on to detect speech/end of speech.
    if (num_samples_recorded_ >= (kEndpointerEstimationTimeMs *
                                  kAudioSampleRate) / 1000) {
      endpointer_.SetUserInputMode();
      delegate_->DidCompleteEnvironmentEstimation(caller_id_);
    }
    return;  // No more processing since we are still estimating environment.
  }

  // Check if we have waited too long without hearing any speech.
  if (!endpointer_.DidStartReceivingSpeech() &&
      num_samples_recorded_ >= kNoSpeechTimeoutSec * kAudioSampleRate) {
    InformErrorAndCancelRecognition(RECOGNIZER_ERROR_NO_SPEECH);
    return;
  }

  // Calculate the input volume to display in the UI, smoothing towards the
  // new level.
  float level = (rms - kAudioMeterMinDb) / kAudioMeterDbRange;
  level = std::min(std::max(0.0f, level), 1.0f);
  if (level > audio_level_) {
    audio_level_ += (level - audio_level_) * kUpSmoothingFactor;
  } else {
    audio_level_ += (level - audio_level_) * kDownSmoothingFactor;
  }
  delegate_->SetInputVolume(caller_id_, audio_level_);

  if (endpointer_.speech_input_complete()) {
    StopRecording();
  }

  // TODO(satish): Once we have streaming POST, start sending the data received
  // here as POST chunks.
}

void SpeechRecognizer::SetRecognitionResult(
    bool error, const SpeechInputResultArray& result) {
  if (result.empty()) {
    InformErrorAndCancelRecognition(RECOGNIZER_ERROR_NO_RESULTS);
    return;
  }

  delegate_->SetRecognitionResult(caller_id_, error, result);

  // Guard against the delegate freeing us until we finish our job.
  scoped_refptr<SpeechRecognizer> me(this);
  delegate_->DidCompleteRecognition(caller_id_);
}

void SpeechRecognizer::InformErrorAndCancelRecognition(ErrorCode error) {
  CancelRecognition();

  // Guard against the delegate freeing us until we finish our job.
  scoped_refptr<SpeechRecognizer> me(this);
  delegate_->OnRecognizerError(caller_id_, error);
}

}  // namespace speech_input
