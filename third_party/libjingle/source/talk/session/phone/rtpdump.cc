/*
 * libjingle
 * Copyright 2010, Google Inc.
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

#include "talk/session/phone/rtpdump.h"

#include <string>

#include "talk/base/bytebuffer.h"
#include "talk/base/logging.h"
#include "talk/base/time.h"

namespace cricket {

static const char kRtpDumpFileFirstLine[] = "#!rtpplay1.0 0.0.0.0/0\n";

struct RtpDumpFileHeader {
  RtpDumpFileHeader(uint32 start_ms, uint32 s, uint16 p)
      : start_sec(start_ms / 1000),
        start_usec(start_ms % 1000 * 1000),
        source(s),
        port(p),
        padding(0) {
  }

  void WriteToByteBuffer(talk_base::ByteBuffer* buf) {
    buf->WriteUInt32(start_sec);
    buf->WriteUInt32(start_usec);
    buf->WriteUInt32(source);
    buf->WriteUInt16(port);
    buf->WriteUInt16(padding);
  }

  static const size_t kHeaderLength = 16;
  uint32 start_sec;   // start of recording, the seconds part.
  uint32 start_usec;  // start of recording, the microseconds part.
  uint32 source;      // network source (multicast address).
  uint16 port;        // UDP port.
  uint16 padding;     // 2 bytes padding.
};

// RTP packet format (http://www.networksorcery.com/enp/protocol/rtp.htm).
static const int kRtpSeqNumOffset = 2;
static const int kRtpSeqNumAndTimestampSize = 6;
static const uint32 kDefaultTimeIncrease = 30;

bool RtpDumpPacket::IsValidRtpPacket() const {
  return !is_rtcp &&
      data.size() >= kRtpSeqNumOffset + kRtpSeqNumAndTimestampSize;
}

///////////////////////////////////////////////////////////////////////////
// Implementation of RtpDumpReader.
///////////////////////////////////////////////////////////////////////////
talk_base::StreamResult RtpDumpReader::ReadPacket(RtpDumpPacket* packet) {
  if (!packet) return talk_base::SR_ERROR;

  talk_base::StreamResult res = talk_base::SR_SUCCESS;
  // Read the file header if it has not been read yet.
  if (!file_header_read_) {
    res = ReadFileHeader();
    if (res != talk_base::SR_SUCCESS) {
      return res;
    }
    file_header_read_ = true;
  }

  // Read the RTP dump packet header.
  char header[RtpDumpPacket::kHeaderLength];
  res = stream_->ReadAll(header, sizeof(header), NULL, NULL);
  if (res != talk_base::SR_SUCCESS) {
    return res;
  }
  talk_base::ByteBuffer buf(header, sizeof(header));
  uint16 dump_packet_len;
  uint16 data_len;
  buf.ReadUInt16(&dump_packet_len);
  buf.ReadUInt16(&data_len);  // data.size() for RTP, 0 for RTCP.
  packet->is_rtcp = (0 == data_len);
  buf.ReadUInt32(&packet->elapsed_time);
  packet->data.resize(dump_packet_len - sizeof(header));

  // Read the actual RTP or RTCP packet.
  return stream_->ReadAll(&packet->data[0], packet->data.size(), NULL, NULL);
}

talk_base::StreamResult RtpDumpReader::ReadFileHeader() {
  // Read the first line.
  std::string first_line;
  talk_base::StreamResult res = stream_->ReadLine(&first_line);
  if (res != talk_base::SR_SUCCESS) {
    return res;
  }
  if (!CheckFirstLine(first_line)) {
    return talk_base::SR_ERROR;
  }

  // Read the 16 byte file header.
  char header[RtpDumpFileHeader::kHeaderLength];
  res = stream_->ReadAll(header, sizeof(header), NULL, NULL);
  if (res == talk_base::SR_SUCCESS) {
    talk_base::ByteBuffer buf(header, sizeof(header));
    uint32 start_sec;
    uint32 start_usec;
    buf.ReadUInt32(&start_sec);
    buf.ReadUInt32(&start_usec);
    start_time_ms_ = start_sec * 1000 + start_usec / 1000;
    // Increase the length by 1 since first_line does not contain the ending \n.
    first_line_and_file_header_len_ = first_line.size() + 1 + sizeof(header);
  }
  return res;
}

bool RtpDumpReader::CheckFirstLine(const std::string& first_line) {
  // The first line is like "#!rtpplay1.0 address/port"
  bool matched = (0 == first_line.find("#!rtpplay1.0 "));

  // The address could be IP or hostname. We do not check it here. Instead, we
  // check the port at the end.
  size_t pos = first_line.find('/');
  matched &= (pos != std::string::npos && pos < first_line.size() - 1);
  for (++pos; pos < first_line.size() && matched; ++pos) {
    matched &= (0 != isdigit(first_line[pos]));
  }

  return matched;
}

///////////////////////////////////////////////////////////////////////////
// Implementation of RtpDumpLoopReader.
///////////////////////////////////////////////////////////////////////////
RtpDumpLoopReader::RtpDumpLoopReader(talk_base::StreamInterface* stream)
    : RtpDumpReader(stream),
      loop_count_(0),
      elapsed_time_increases_(0),
      rtp_seq_num_increase_(0),
      rtp_timestamp_increase_(0),
      packet_count_(0),
      frame_count_(0),
      first_elapsed_time_(0),
      first_rtp_seq_num_(0),
      first_rtp_timestamp_(0),
      prev_elapsed_time_(0),
      prev_rtp_seq_num_(0),
      prev_rtp_timestamp_(0) {
}

talk_base::StreamResult RtpDumpLoopReader::ReadPacket(RtpDumpPacket* packet) {
  if (!packet) return talk_base::SR_ERROR;

  talk_base::StreamResult res = RtpDumpReader::ReadPacket(packet);
  if (talk_base::SR_SUCCESS == res) {
    if (0 == loop_count_) {
      // During the first loop, we update the statistics of the input stream.
      UpdateStreamStatistics(*packet);
    }
  } else if (talk_base::SR_EOS == res) {
    if (0 == loop_count_) {
      // At the end of the first loop, calculate elapsed_time_increases_,
      // rtp_seq_num_increase_, and rtp_timestamp_increase_, which will be
      // used during the second and later loops.
      CalculateIncreases();
    }

    // Rewind the input stream to the first dump packet and read again.
    ++loop_count_;
    if (RewindToFirstDumpPacket()) {
      res = RtpDumpReader::ReadPacket(packet);
    }
  }

  if (talk_base::SR_SUCCESS == res && loop_count_ > 0) {
    // During the second and later loops, we update the elapsed time of the dump
    // packet. If the dumped packet is a RTP packet, we also update its RTP
    // sequence number and timestamp.
    UpdateDumpPacket(packet);
  }

  return res;
}

void RtpDumpLoopReader::UpdateStreamStatistics(const RtpDumpPacket& packet) {
  // Get the RTP sequence number and timestamp of the dump packet.
  uint16 rtp_seq_num = 0;
  uint32 rtp_timestamp = 0;
  if (packet.IsValidRtpPacket()) {
    ReadRtpSeqNumAndTimestamp(packet, &rtp_seq_num, &rtp_timestamp);
  }

  // Get the timestamps and sequence number for the first dump packet.
  if (0 == packet_count_++) {
    first_elapsed_time_ = packet.elapsed_time;
    first_rtp_seq_num_ = rtp_seq_num;
    first_rtp_timestamp_ = rtp_timestamp;
    // The first packet belongs to a new payload frame.
    ++frame_count_;
  } else if (rtp_timestamp != prev_rtp_timestamp_) {
    // The current and previous packets belong to different payload frames.
    ++frame_count_;
  }

  prev_elapsed_time_ = packet.elapsed_time;
  prev_rtp_timestamp_ = rtp_timestamp;
  prev_rtp_seq_num_ = rtp_seq_num;
}

void RtpDumpLoopReader::CalculateIncreases() {
  // At this time, prev_elapsed_time_, prev_rtp_seq_num_, and
  // prev_rtp_timestamp_ are values of the last dump packet in the input stream.
  rtp_seq_num_increase_ = prev_rtp_seq_num_ - first_rtp_seq_num_ + 1;
  // If we have only one packet or frame, we use the default timestamp
  // increase. Otherwise, we use the difference between the first and the last
  // packets or frames.
  elapsed_time_increases_ = packet_count_ <= 1 ? kDefaultTimeIncrease :
      (prev_elapsed_time_ - first_elapsed_time_) * packet_count_ /
      (packet_count_ - 1);
  rtp_timestamp_increase_ = frame_count_ <= 1 ? kDefaultTimeIncrease :
      (prev_rtp_timestamp_ - first_rtp_timestamp_) * frame_count_ /
      (frame_count_ - 1);
}

void RtpDumpLoopReader::UpdateDumpPacket(RtpDumpPacket* packet) {
  // Increase the elapsed time of the dump packet.
  packet->elapsed_time += loop_count_ * elapsed_time_increases_;

  if (packet->IsValidRtpPacket()) {
    // Get the old RTP sequence number and timestamp.
    uint16 sequence;
    uint32 timestamp;
    ReadRtpSeqNumAndTimestamp(*packet, &sequence, &timestamp);
    // Increase the RTP sequence number and timestamp.
    sequence += loop_count_ * rtp_seq_num_increase_;
    timestamp += loop_count_ * rtp_timestamp_increase_;
    // Write the updated sequence number and timestamp back to the RTP packet.
    talk_base::ByteBuffer buffer;
    buffer.WriteUInt16(sequence);
    buffer.WriteUInt32(timestamp);
    memcpy(&packet->data[0] + kRtpSeqNumOffset, buffer.Data(), buffer.Length());
  }
}

void RtpDumpLoopReader::ReadRtpSeqNumAndTimestamp(
    const RtpDumpPacket& packet, uint16* sequence, uint32* timestamp) {
  talk_base::ByteBuffer buffer(
      reinterpret_cast<const char*>(&packet.data[0] + kRtpSeqNumOffset),
      kRtpSeqNumAndTimestampSize);
  buffer.ReadUInt16(sequence);
  buffer.ReadUInt32(timestamp);
}

///////////////////////////////////////////////////////////////////////////
// Implementation of RtpDumpWriter.
///////////////////////////////////////////////////////////////////////////
uint32 RtpDumpWriter::GetElapsedTime() const {
  return talk_base::TimeSince(start_time_ms_);
}

talk_base::StreamResult RtpDumpWriter::WritePacket(
    const void* data, size_t data_len, uint32 elapsed, bool rtcp) {
  if (!data || 0 == data_len) return talk_base::SR_ERROR;

  talk_base::StreamResult res = talk_base::SR_SUCCESS;
  // Write the file header if it has not been written yet.
  if (!file_header_written_) {
    res = WriteFileHeader();
    if (res != talk_base::SR_SUCCESS) {
      return res;
    }
    file_header_written_ = true;
  }

  // Write the dump packet header.
  talk_base::ByteBuffer buf;
  buf.WriteUInt16(static_cast<uint16>(RtpDumpPacket::kHeaderLength + data_len));
  buf.WriteUInt16(static_cast<uint16>(rtcp ? 0 : data_len));
  buf.WriteUInt32(elapsed);
  res = stream_->WriteAll(buf.Data(), buf.Length(), NULL, NULL);
  if (res != talk_base::SR_SUCCESS) {
    return res;
  }

  // Write the actual RTP or RTCP packet.
  return stream_->WriteAll(data, data_len, NULL, NULL);
}

talk_base::StreamResult RtpDumpWriter::WriteFileHeader() {
  talk_base::StreamResult res = stream_->WriteAll(
      kRtpDumpFileFirstLine, strlen(kRtpDumpFileFirstLine), NULL, NULL);
  if (res != talk_base::SR_SUCCESS) {
    return res;
  }

  talk_base::ByteBuffer buf;
  RtpDumpFileHeader file_header(talk_base::Time(), 0, 0);
  file_header.WriteToByteBuffer(&buf);
  return stream_->WriteAll(buf.Data(), buf.Length(), NULL, NULL);
}

}  // namespace cricket
