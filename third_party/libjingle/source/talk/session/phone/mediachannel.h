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

#ifndef TALK_SESSION_PHONE_MEDIACHANNEL_H_
#define TALK_SESSION_PHONE_MEDIACHANNEL_H_

#include <string>
#include <vector>

#include "talk/base/basictypes.h"
#include "talk/base/sigslot.h"
#include "talk/base/socket.h"
#include "talk/session/phone/codec.h"
// TODO: re-evaluate this include
#include "talk/session/phone/audiomonitor.h"

namespace flute {
  class MagicCamVideoRenderer;
}

namespace cricket {

enum VoiceMediaChannelOptions {
  OPT_CONFERENCE = 0x10000,   // tune the audio stream for conference mode
  OPT_ENERGYLEVEL = 0x20000,  // include the energy level in RTP packets, as
                              // defined in https://datatracker.ietf.org/drafts/
                              // draft-lennox-avt-rtp-audio-level-exthdr/

};

enum VideoMediaChannelOptions {
};

class MediaChannel : public sigslot::has_slots<> {
 public:
  class NetworkInterface {
   public:
    enum SocketType { ST_RTP, ST_RTCP };
    virtual int SendPacket(const void *data, size_t len) = 0;
    virtual int SendRtcp(const void *data, size_t len) = 0;
    virtual int SetOption(SocketType type, talk_base::Socket::Option opt,
                          int option) = 0;
    virtual ~NetworkInterface() {}
  };

  MediaChannel() : network_interface_(NULL) {}
  virtual ~MediaChannel() {}

  // Gets/sets the abstract inteface class for sending RTP/RTCP data.
  NetworkInterface *network_interface() { return network_interface_; }
  virtual void SetInterface(NetworkInterface *iface) {
    network_interface_ = iface;
  }

  // Called when a RTP packet is received.
  virtual void OnPacketReceived(const void *data, int len) = 0;
  // Called when a RTCP packet is received.
  virtual void OnRtcpReceived(const void *data, int len) = 0;
  // Sets the SSRC to be used for outgoing data.
  virtual void SetSendSsrc(uint32 id) = 0;
  // Set the CNAME of RTCP
  virtual bool SetRtcpCName(const std::string& cname) = 0;
  // Mutes the channel.
  virtual bool Mute(bool on) = 0;

  virtual bool SetRtpExtensionHeaders(bool enable_all) { return true; }
  virtual bool SetMaxSendBandwidth(int max_bandwidth) = 0;
  virtual bool SetOptions(int options) = 0;

 protected:
  NetworkInterface *network_interface_;
};

enum SendFlags {
  SEND_NOTHING,
  SEND_RINGBACKTONE,
  SEND_MICROPHONE
};

// TODO: separate into VoiceMediaInfo and VideoMediaInfo
struct MediaInfo {
  int fraction_lost;
  int cum_lost;
  int ext_max;
  int jitter;
  int RTT;
  int bytesSent;
  int packetsSent;
  int bytesReceived;
  int packetsReceived;
};

typedef MediaInfo VoiceMediaInfo;
typedef MediaInfo VideoMediaInfo;

class VoiceMediaChannel : public MediaChannel {
 public:
  VoiceMediaChannel() {}
  virtual ~VoiceMediaChannel() {}
  // Sets the codecs/payload types to be used for incoming media.
  virtual bool SetRecvCodecs(const std::vector<AudioCodec>& codecs) = 0;
  // Sets the codecs/payload types to be used for outgoing media.
  virtual bool SetSendCodecs(const std::vector<AudioCodec>& codecs) = 0;
  // Starts or stops playout of received audio.
  virtual bool SetPlayout(bool playout) = 0;
  // Starts or stops sending (and potentially capture) of local audio.
  virtual bool SetSend(SendFlags flag) = 0;
  // Adds a new receive-only stream with the specified SSRC.
  virtual bool AddStream(uint32 ssrc) = 0;
  // Removes a stream added with AddStream.
  virtual bool RemoveStream(uint32 ssrc) = 0;
  // Gets current energy levels for all incoming streams.
  virtual bool GetActiveStreams(AudioInfo::StreamList* actives) = 0;
  // Get the current energy level for the outgoing stream.
  virtual int GetOutputLevel() = 0;
  // Specifies a ringback tone to be played during call setup.
  virtual void SetRingbackTone(const char *buf, int len) = 0;
  // Plays or stops the aforementioned ringback tone
  virtual bool PlayRingbackTone(bool play, bool loop) = 0;
  // Sends a out-of-band DTMF signal using the specified event.
  virtual bool PressDTMF(int event, bool playout) = 0;
  // Gets quality stats for the channel.
  virtual bool GetStats(VoiceMediaInfo* info) = 0;
};

// Represents a YUV420 (a.k.a. I420) video frame.
class VideoFrame {
  friend class flute::MagicCamVideoRenderer;

 public:
  VideoFrame() : rendered_(false) {}

  virtual ~VideoFrame() {}

  virtual size_t GetWidth() const = 0;
  virtual size_t GetHeight() const = 0;
  virtual const uint8 *GetYPlane() const = 0;
  virtual const uint8 *GetUPlane() const = 0;
  virtual const uint8 *GetVPlane() const = 0;
  virtual uint8 *GetYPlane() = 0;
  virtual uint8 *GetUPlane() = 0;
  virtual uint8 *GetVPlane() = 0;
  virtual int32 GetYPitch() const = 0;
  virtual int32 GetUPitch() const = 0;
  virtual int32 GetVPitch() const = 0;

  // For retrieving the aspect ratio of each pixel. Usually this is 1x1, but
  // the aspect_ratio_idc parameter of H.264 can specify non-square pixels.
  virtual size_t GetPixelWidth() const = 0;
  virtual size_t GetPixelHeight() const = 0;

  // TODO: Add a fourcc format here and probably combine VideoFrame
  // with CapturedFrame.
  virtual int64 GetElapsedTime() const = 0;
  virtual int64 GetTimeStamp() const = 0;
  virtual void SetElapsedTime(int64 elapsed_time) = 0;
  virtual void SetTimeStamp(int64 time_stamp) = 0;

  // Writes the frame into the given frame buffer, provided that it is of
  // sufficient size. Returns the frame's actual size, regardless of whether
  // it was written or not (like snprintf). If there is insufficient space,
  // nothing is written.
  virtual size_t CopyToBuffer(uint8 *buffer, size_t size) const = 0;

  // Converts the I420 data to RGB of a certain type such as BGRA and RGBA.
  // Returns the frame's actual size, regardless of whether it was written or
  // not (like snprintf). Parameters size and pitch_rgb are in units of bytes.
  // If there is insufficient space, nothing is written.
  virtual size_t ConvertToRgbBuffer(uint32 to_fourcc, uint8 *buffer,
                                    size_t size, size_t pitch_rgb) const = 0;

  // Writes the frame into the given planes, stretched to the given width and
  // height. The parameter "interpolate" controls whether to interpolate or just
  // take the nearest-point. The parameter "crop" controls whether to crop this
  // frame to the aspect ratio of the given dimensions before stretching.
  virtual void StretchToPlanes(uint8 *y, uint8 *u, uint8 *v,
                               int32 pitchY, int32 pitchU, int32 pitchV,
                               size_t width, size_t height,
                               bool interpolate, bool crop) const = 0;

  // Writes the frame into the given frame buffer, stretched to the given width
  // and height, provided that it is of sufficient size. Returns the frame's
  // actual size, regardless of whether it was written or not (like snprintf).
  // If there is insufficient space, nothing is written. The parameter
  // "interpolate" controls whether to interpolate or just take the
  // nearest-point. The parameter "crop" controls whether to crop this frame to
  // the aspect ratio of the given dimensions before stretching.
  virtual size_t StretchToBuffer(size_t w, size_t h, uint8 *buffer, size_t size,
                                 bool interpolate, bool crop) const = 0;

  // Writes the frame into the target VideoFrame, stretched to the size of that
  // frame. The parameter "interpolate" controls whether to interpolate or just
  // take the nearest-point. The parameter "crop" controls whether to crop this
  // frame to the aspect ratio of the target frame before stretching.
  virtual void StretchToFrame(VideoFrame *target, bool interpolate,
                              bool crop) const = 0;

  // Stretches the frame to the given size, creating a new VideoFrame object to
  // hold it. The parameter "interpolate" controls whether to interpolate or
  // just take the nearest-point. The parameter "crop" controls whether to crop
  // this frame to the aspect ratio of the given dimensions before stretching.
  virtual VideoFrame *Stretch(size_t w, size_t h, bool interpolate,
                              bool crop) const = 0;

  // Size of an I420 image of given dimensions when stored as a frame buffer.
  static size_t SizeOf(size_t w, size_t h) {
    return w * h * 3 / 2;
  }

 protected:
  // The frame needs to be rendered to magiccam only once.
  // TODO: Remove this flag once magiccam rendering is fully replaced
  // by client3d rendering.
  mutable bool rendered_;
};

// Simple subclass for use in mocks.
class NullVideoFrame : public VideoFrame {
 public:
  virtual size_t GetWidth() const { return 0; }
  virtual size_t GetHeight() const { return 0; }
  virtual const uint8 *GetYPlane() const { return NULL; }
  virtual const uint8 *GetUPlane() const { return NULL; }
  virtual const uint8 *GetVPlane() const { return NULL; }
  virtual uint8 *GetYPlane() { return NULL; }
  virtual uint8 *GetUPlane() { return NULL; }
  virtual uint8 *GetVPlane() { return NULL; }
  virtual int32 GetYPitch() const { return 0; }
  virtual int32 GetUPitch() const { return 0; }
  virtual int32 GetVPitch() const { return 0; }

  virtual size_t GetPixelWidth() const { return 1; }
  virtual size_t GetPixelHeight() const { return 1; }
  virtual int64 GetElapsedTime() const { return 0; }
  virtual int64 GetTimeStamp() const { return 0; }
  virtual void SetElapsedTime(int64 elapsed_time) {}
  virtual void SetTimeStamp(int64 time_stamp) {}

  virtual size_t CopyToBuffer(uint8 *buffer, size_t size) const {
    return 0;
  }

  virtual size_t ConvertToRgbBuffer(uint32 to_fourcc, uint8 *buffer,
                                    size_t size, size_t pitch_rgb) const {
    return 0;
  }

  virtual void StretchToPlanes(uint8 *y, uint8 *u, uint8 *v,
                               int32 pitchY, int32 pitchU, int32 pitchV,
                               size_t width, size_t height,
                               bool interpolate, bool crop) const {
  }

  virtual size_t StretchToBuffer(size_t w, size_t h, uint8 *buffer, size_t size,
                                 bool interpolate, bool crop) const {
    return 0;
  }

  virtual void StretchToFrame(VideoFrame *target, bool interpolate,
                              bool crop) const {
  }

  virtual VideoFrame *Stretch(size_t w, size_t h, bool interpolate,
                              bool crop) const {
    return NULL;
  }
};

// Abstract interface for rendering VideoFrames.
class VideoRenderer {
 public:
  virtual ~VideoRenderer() {}
  // Called when the video has changed size.
  virtual bool SetSize(int width, int height, int reserved) = 0;
  // Called when a new frame is available for display.
  virtual bool RenderFrame(const VideoFrame *frame) = 0;
};

// Simple implementation for use in tests.
class NullVideoRenderer : public VideoRenderer {
  virtual bool SetSize(int width, int height, int reserved) {
    return true;
  }
  // Called when a new frame is available for display.
  virtual bool RenderFrame(const VideoFrame *frame) {
    return true;
  }
};

class VideoMediaChannel : public MediaChannel {
 public:
  VideoMediaChannel() { renderer_ = NULL; }
  virtual ~VideoMediaChannel() {}
  // Sets the codecs/payload types to be used for incoming media.
  virtual bool SetRecvCodecs(const std::vector<VideoCodec> &codecs) = 0;
  // Sets the codecs/payload types to be used for outgoing media.
  virtual bool SetSendCodecs(const std::vector<VideoCodec> &codecs) = 0;
  // Starts or stops playout of received video.
  virtual bool SetRender(bool render) = 0;
  // Starts or stops transmission (and potentially capture) of local video.
  virtual bool SetSend(bool send) = 0;
  // Adds a new receive-only stream with the specified SSRC.
  virtual bool AddStream(uint32 ssrc, uint32 voice_ssrc) = 0;
  // Removes a stream added with AddStream.
  virtual bool RemoveStream(uint32 ssrc) = 0;
  // Sets the renderer object to be used for the specified stream.
  // If SSRC is 0, the renderer is used for the 'default' stream.
  virtual bool SetRenderer(uint32 ssrc, VideoRenderer* renderer) = 0;
  // Gets quality stats for the channel.
  virtual bool GetStats(VideoMediaInfo* info) = 0;
 protected:
  VideoRenderer *renderer_;
};

}  // namespace cricket

#endif  // TALK_SESSION_PHONE_MEDIACHANNEL_H_
