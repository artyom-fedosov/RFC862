// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cerrno>
#include <cstddef>

#include <gtest/gtest.h>

import rfc862;
import rfc862.test.fake_stream_io;

namespace {

using rfc862::test::bytes;
using rfc862::test::FakeStreamIo;
using Connection = rfc862::BasicTcpEchoConnection<FakeStreamIo>;

class FakeConnection : public ::testing::Test {
  protected:
    Connection connection{rfc862::FileDescriptor{}};
    std::array<std::byte, 64> scratch{};

    FakeStreamIo& io() { return connection.io(); }
};

}  // namespace

TEST_F(FakeConnection, EchoesReadBytesAndWaitsForMore) {
    // Given
    io().script_read("abc");
    // When
    const auto progress = connection.on_readable(scratch);
    // Then
    EXPECT_EQ(progress, rfc862::Progress::WantRead);
    EXPECT_EQ(io().written(), bytes("abc"));
    EXPECT_EQ(connection.pending_bytes(), 0U);
}

TEST_F(FakeConnection, KeepsUnwrittenBytesPendingOnShortWrite) {
    // Given
    io().script_read("hello");
    io().script_write({.max_bytes = 2});
    io().script_write({.error = EAGAIN});
    // When
    const auto progress = connection.on_readable(scratch);
    // Then
    EXPECT_EQ(progress, rfc862::Progress::WantWrite);
    EXPECT_EQ(io().written(), bytes("he"));
    EXPECT_EQ(connection.pending_bytes(), 3U);
}

TEST_F(FakeConnection, FlushesPendingBytesInOrderWhenWritable) {
    // Given
    io().script_read("hello");
    io().script_write({.max_bytes = 2});
    io().script_write({.error = EAGAIN});
    ASSERT_EQ(connection.on_readable(scratch), rfc862::Progress::WantWrite);
    // When
    const auto progress = connection.on_writable();
    // Then
    EXPECT_EQ(progress, rfc862::Progress::WantRead);
    EXPECT_EQ(io().written(), bytes("hello"));
    EXPECT_EQ(connection.pending_bytes(), 0U);
}

TEST_F(FakeConnection, KeepsWaitingWhilePendingBytesCannotBeWritten) {
    // Given
    io().script_read("hello");
    io().script_write({.max_bytes = 1});
    io().script_write({.error = EAGAIN});
    ASSERT_EQ(connection.on_readable(scratch), rfc862::Progress::WantWrite);
    io().script_write({.max_bytes = 2});
    io().script_write({.error = EAGAIN});
    // When
    const auto progress = connection.on_writable();
    // Then
    EXPECT_EQ(progress, rfc862::Progress::WantWrite);
    EXPECT_EQ(io().written(), bytes("hel"));
    EXPECT_EQ(connection.pending_bytes(), 2U);
}

TEST_F(FakeConnection, ClosesWhenPeerFinishesSending) {
    // Given
    io().script_read("bye");
    io().script_eof();
    // When
    const auto progress = connection.on_readable(scratch);
    // Then
    EXPECT_EQ(progress, rfc862::Progress::Closed);
    EXPECT_EQ(io().written(), bytes("bye"));
}

TEST_F(FakeConnection, ClosesOnReadError) {
    // Given
    io().script_read_error(ECONNRESET);
    // When
    const auto progress = connection.on_readable(scratch);
    // Then
    EXPECT_EQ(progress, rfc862::Progress::Closed);
}

TEST_F(FakeConnection, RetriesInterruptedRead) {
    // Given
    io().script_read_error(EINTR);
    io().script_read("ok");
    // When
    const auto progress = connection.on_readable(scratch);
    // Then
    EXPECT_EQ(progress, rfc862::Progress::WantRead);
    EXPECT_EQ(io().written(), bytes("ok"));
}

TEST_F(FakeConnection, ClosesOnWriteError) {
    // Given
    io().script_read("data");
    io().script_write({.error = EPIPE});
    // When
    const auto progress = connection.on_readable(scratch);
    // Then
    EXPECT_EQ(progress, rfc862::Progress::Closed);
}

TEST_F(FakeConnection, ClosesOnWriteErrorWhileFlushing) {
    // Given
    io().script_read("data");
    io().script_write({.error = EAGAIN});
    ASSERT_EQ(connection.on_readable(scratch), rfc862::Progress::WantWrite);
    io().script_write({.error = ECONNRESET});
    // When
    const auto progress = connection.on_writable();
    // Then
    EXPECT_EQ(progress, rfc862::Progress::Closed);
}

TEST_F(FakeConnection, RetriesInterruptedWrite) {
    // Given
    io().script_read("data");
    io().script_write({.error = EINTR});
    // When
    const auto progress = connection.on_readable(scratch);
    // Then
    EXPECT_EQ(progress, rfc862::Progress::WantRead);
    EXPECT_EQ(io().written(), bytes("data"));
}
