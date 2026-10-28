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
