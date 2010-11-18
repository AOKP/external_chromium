// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/spdy_session.h"

#include "net/spdy/spdy_io_buffer.h"
#include "net/spdy/spdy_stream.h"
#include "net/spdy/spdy_test_util.h"
#include "testing/platform_test.h"

namespace net {

// TODO(cbentzel): Expose compression setter/getter in public SpdySession
//                 interface rather than going through all these contortions.
class SpdySessionTest : public PlatformTest {
 public:
  static void TurnOffCompression() {
    spdy::SpdyFramer::set_enable_compression_default(false);
  }
};

namespace {

// Test the SpdyIOBuffer class.
TEST_F(SpdySessionTest, SpdyIOBuffer) {
  std::priority_queue<SpdyIOBuffer> queue_;
  const size_t kQueueSize = 100;

  // Insert 100 items; pri 100 to 1.
  for (size_t index = 0; index < kQueueSize; ++index) {
    SpdyIOBuffer buffer(new IOBuffer(), 0, kQueueSize - index, NULL);
    queue_.push(buffer);
  }

  // Insert several priority 0 items last.
  const size_t kNumDuplicates = 12;
  IOBufferWithSize* buffers[kNumDuplicates];
  for (size_t index = 0; index < kNumDuplicates; ++index) {
    buffers[index] = new IOBufferWithSize(index+1);
    queue_.push(SpdyIOBuffer(buffers[index], buffers[index]->size(), 0, NULL));
  }

  EXPECT_EQ(kQueueSize + kNumDuplicates, queue_.size());

  // Verify the P0 items come out in FIFO order.
  for (size_t index = 0; index < kNumDuplicates; ++index) {
    SpdyIOBuffer buffer = queue_.top();
    EXPECT_EQ(0, buffer.priority());
    EXPECT_EQ(index + 1, buffer.size());
    queue_.pop();
  }

  int priority = 1;
  while (queue_.size()) {
    SpdyIOBuffer buffer = queue_.top();
    EXPECT_EQ(priority++, buffer.priority());
    queue_.pop();
  }
}

TEST_F(SpdySessionTest, GoAway) {
  SpdySessionDependencies session_deps;
  session_deps.host_resolver->set_synchronous_mode(true);

  MockConnect connect_data(false, OK);
  scoped_ptr<spdy::SpdyFrame> goaway(ConstructSpdyGoAway());
  MockRead reads[] = {
    CreateMockRead(*goaway),
    MockRead(false, 0, 0)  // EOF
  };
  StaticSocketDataProvider data(reads, arraysize(reads), NULL, 0);
  data.set_connect_data(connect_data);
  session_deps.socket_factory->AddSocketDataProvider(&data);

  SSLSocketDataProvider ssl(false, OK);
  session_deps.socket_factory->AddSSLSocketDataProvider(&ssl);

  scoped_refptr<HttpNetworkSession> http_session(
      SpdySessionDependencies::SpdyCreateSession(&session_deps));

  const std::string kTestHost("www.foo.com");
  const int kTestPort = 80;
  HostPortPair test_host_port_pair(kTestHost, kTestPort);
  HostPortProxyPair pair(test_host_port_pair, ProxyServer::Direct());

  SpdySessionPool* spdy_session_pool(http_session->spdy_session_pool());
  EXPECT_FALSE(spdy_session_pool->HasSession(pair));
  scoped_refptr<SpdySession> session =
      spdy_session_pool->Get(pair, http_session->mutable_spdy_settings(),
                             BoundNetLog());
  EXPECT_TRUE(spdy_session_pool->HasSession(pair));

  scoped_refptr<TCPSocketParams> tcp_params(
      new TCPSocketParams(kTestHost, kTestPort, MEDIUM, GURL(), false));
  scoped_ptr<ClientSocketHandle> connection(new ClientSocketHandle);
  EXPECT_EQ(OK,
            connection->Init(test_host_port_pair.ToString(), tcp_params, MEDIUM,
                              NULL, http_session->tcp_socket_pool(),
                              BoundNetLog()));
  EXPECT_EQ(OK, session->InitializeWithSocket(connection.release(), false, OK));

  // Flush the SpdySession::OnReadComplete() task.
  MessageLoop::current()->RunAllPending();

  EXPECT_FALSE(spdy_session_pool->HasSession(pair));

  scoped_refptr<SpdySession> session2 =
      spdy_session_pool->Get(pair, http_session->mutable_spdy_settings(),
                             BoundNetLog());

  // Delete the first session.
  session = NULL;

  // Delete the second session.
  spdy_session_pool->Remove(session2);
  session2 = NULL;
}

class StreamReleaserCallback : public CallbackRunner<Tuple1<int> > {
 public:
  StreamReleaserCallback(SpdySession* session,
                         SpdyStream* first_stream)
      : session_(session), first_stream_(first_stream) {}
  ~StreamReleaserCallback() {}

  int WaitForResult() { return callback_.WaitForResult(); }

  virtual void RunWithParams(const Tuple1<int>& params) {
    session_->CloseSessionOnError(ERR_FAILED, false);
    session_ = NULL;
    first_stream_->Cancel();
    first_stream_ = NULL;
    stream_->Cancel();
    stream_ = NULL;
    callback_.RunWithParams(params);
  }

  scoped_refptr<SpdyStream>* stream() { return &stream_; }

 private:
  scoped_refptr<SpdySession> session_;
  scoped_refptr<SpdyStream> first_stream_;
  scoped_refptr<SpdyStream> stream_;
  TestCompletionCallback callback_;
};

// Start with max concurrent streams set to 1.  Request two streams.  Receive a
// settings frame setting max concurrent streams to 2.  Have the callback
// release the stream, which releases its reference (the last) to the session.
// Make sure nothing blows up.
// http://crbug.com/57331
TEST_F(SpdySessionTest, OnSettings) {
  SpdySessionDependencies session_deps;
  session_deps.host_resolver->set_synchronous_mode(true);

  spdy::SpdySettings new_settings;
  spdy::SettingsFlagsAndId id(spdy::SETTINGS_MAX_CONCURRENT_STREAMS);
  id.set_id(spdy::SETTINGS_MAX_CONCURRENT_STREAMS);
  const size_t max_concurrent_streams = 2;
  new_settings.push_back(spdy::SpdySetting(id, max_concurrent_streams));

  // Set up the socket so we read a SETTINGS frame that raises max concurrent
  // streams to 2.
  MockConnect connect_data(false, OK);
  scoped_ptr<spdy::SpdyFrame> settings_frame(
      ConstructSpdySettings(new_settings));
  MockRead reads[] = {
    CreateMockRead(*settings_frame),
    MockRead(false, 0, 0)  // EOF
  };

  StaticSocketDataProvider data(reads, arraysize(reads), NULL, 0);
  data.set_connect_data(connect_data);
  session_deps.socket_factory->AddSocketDataProvider(&data);

  SSLSocketDataProvider ssl(false, OK);
  session_deps.socket_factory->AddSSLSocketDataProvider(&ssl);

  scoped_refptr<HttpNetworkSession> http_session(
      SpdySessionDependencies::SpdyCreateSession(&session_deps));

  const std::string kTestHost("www.foo.com");
  const int kTestPort = 80;
  HostPortPair test_host_port_pair(kTestHost, kTestPort);
  HostPortProxyPair pair(test_host_port_pair, ProxyServer::Direct());

  // Initialize the SpdySettingsStorage with 1 max concurrent streams.
  spdy::SpdySettings old_settings;
  id.set_flags(spdy::SETTINGS_FLAG_PLEASE_PERSIST);
  old_settings.push_back(spdy::SpdySetting(id, 1));
  http_session->mutable_spdy_settings()->Set(
      test_host_port_pair, old_settings);

  // Create a session.
  SpdySessionPool* spdy_session_pool(http_session->spdy_session_pool());
  EXPECT_FALSE(spdy_session_pool->HasSession(pair));
  scoped_refptr<SpdySession> session =
      spdy_session_pool->Get(pair, http_session->mutable_spdy_settings(),
                             BoundNetLog());
  ASSERT_TRUE(spdy_session_pool->HasSession(pair));

  scoped_refptr<TCPSocketParams> tcp_params(
      new TCPSocketParams(kTestHost, kTestPort, MEDIUM, GURL(), false));
  scoped_ptr<ClientSocketHandle> connection(new ClientSocketHandle);
  EXPECT_EQ(OK,
            connection->Init(test_host_port_pair.ToString(), tcp_params, MEDIUM,
                              NULL, http_session->tcp_socket_pool(),
                              BoundNetLog()));
  EXPECT_EQ(OK, session->InitializeWithSocket(connection.release(), false, OK));

  // Create 2 streams.  First will succeed.  Second will be pending.
  scoped_refptr<SpdyStream> spdy_stream1;
  TestCompletionCallback callback1;
  GURL url("http://www.google.com");
  EXPECT_EQ(OK,
            session->CreateStream(url,
                                  MEDIUM, /* priority, not important */
                                  &spdy_stream1,
                                  BoundNetLog(),
                                  &callback1));

  StreamReleaserCallback stream_releaser(session, spdy_stream1);

  ASSERT_EQ(ERR_IO_PENDING,
            session->CreateStream(url,
                                  MEDIUM, /* priority, not important */
                                  stream_releaser.stream(),
                                  BoundNetLog(),
                                  &stream_releaser));

  // Make sure |stream_releaser| holds the last refs.
  session = NULL;
  spdy_stream1 = NULL;

  EXPECT_EQ(OK, stream_releaser.WaitForResult());
}

}  // namespace

}  // namespace net
