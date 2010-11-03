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

#ifndef TALK_SESSION_PHONE_CODEC_H_
#define TALK_SESSION_PHONE_CODEC_H_

#include <string>

namespace cricket {

struct AudioCodec {
  int id;
  std::string name;
  int clockrate;
  int bitrate;
  int channels;

  int preference;

  // Creates a codec with the given parameters.
  AudioCodec(int pt, const std::string& nm, int cr, int br, int cs, int pr)
      : id(pt), name(nm), clockrate(cr), bitrate(br),
        channels(cs), preference(pr) {}

  // Creates an empty codec.
  AudioCodec() : id(0), clockrate(0), bitrate(0), channels(0), preference(0) {}

  // Indicates if this codec is compatible with the specified codec.
  bool Matches(int payload, const std::string& nm) const;
  bool Matches(const AudioCodec& codec) const;

  static bool Preferable(const AudioCodec& first, const AudioCodec& other) {
    return first.preference > other.preference;
  }

  std::string ToString() const;

  AudioCodec& operator=(const AudioCodec& c) {
    this->id = c.id;  // id is reserved in objective-c
    name = c.name;
    clockrate = c.clockrate;
    bitrate = c.bitrate;
    channels = c.channels;
    preference =  c.preference;
    return *this;
  }

  bool operator==(const AudioCodec& c) const {
    return this->id == c.id &&  // id is reserved in objective-c
           name == c.name &&
           clockrate == c.clockrate &&
           bitrate == c.bitrate &&
           channels == c.channels &&
           preference == c.preference;
  }

  bool operator!=(const AudioCodec& c) const {
    return !(*this == c);
  }
};

struct VideoCodec {
  int id;
  std::string name;
  int width;
  int height;
  int framerate;

  int preference;

  // Creates a codec with the given parameters.
  VideoCodec(int pt, const std::string& nm, int w, int h, int fr, int pr)
      : id(pt), name(nm), width(w), height(h), framerate(fr), preference(pr) {}

  // Creates an empty codec.
  VideoCodec()
      : id(0), width(0), height(0), framerate(0), preference(0) {}

  bool Matches(int payload, const std::string& nm) const;
  bool Matches(const VideoCodec& codec) const;

  static bool Preferable(const VideoCodec& first, const VideoCodec& other) {
    return first.preference > other.preference;
  }

  std::string ToString() const;

  VideoCodec& operator=(const VideoCodec& c) {
    this->id = c.id;  // id is reserved in objective-c
    name = c.name;
    width = c.width;
    height = c.height;
    framerate = c.framerate;
    preference =  c.preference;
    return *this;
  }

  bool operator==(const VideoCodec& c) const {
    return this->id == c.id &&  // id is reserved in objective-c
           name == c.name &&
           width == c.width &&
           height == c.height &&
           framerate == c.framerate &&
           preference == c.preference;
  }

  bool operator!=(const VideoCodec& c) const {
    return !(*this == c);
  }
};

struct VideoEncoderConfig {
  static const int kDefaultMaxThreads = -1;
  static const int kDefaultCpuProfile = -1;

  VideoEncoderConfig()
      : max_codec(),
        num_threads(kDefaultMaxThreads),
        cpu_profile(kDefaultCpuProfile) {
  }

  VideoEncoderConfig(const VideoCodec& c)
      : max_codec(c),
        num_threads(kDefaultMaxThreads),
        cpu_profile(kDefaultCpuProfile) {
  }

  VideoEncoderConfig(const VideoCodec& c, int t, int p)
      : max_codec(c),
        num_threads(t),
        cpu_profile(p) {
  }

  VideoEncoderConfig& operator=(const VideoEncoderConfig& config) {
    max_codec = config.max_codec;
    num_threads = config.num_threads;
    cpu_profile = config.cpu_profile;
    return *this;
  }

  bool operator==(const VideoEncoderConfig& config) const {
    return max_codec == config.max_codec &&
           num_threads == config.num_threads &&
           cpu_profile == config.cpu_profile;
  }

  bool operator!=(const VideoEncoderConfig& config) const {
    return !(*this == config);
  }

  VideoCodec max_codec;
  int num_threads;
  int cpu_profile;
};

}  // namespace cricket

#endif  // TALK_SESSION_PHONE_CODEC_H_
