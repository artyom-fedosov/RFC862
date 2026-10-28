// SPDX-License-Identifier: GPL-3.0-or-later

#include <cerrno>
#include <cstdint>

#include <gtest/gtest.h>

import rfc862;
import rfc862.test.fake_datagram_io;
import rfc862.test.fake_stream_io;

namespace {

using rfc862::test::bytes;
using rfc862::test::FakeDatagramIo;
using rfc862::test::ipv6_peer;
using rfc862::test::same_peer;

class FakeUdpEcho : public ::testing::Test {
  protected:
    rfc862::BasicUdpEchoBatch<FakeDatagramIo> batch;

    FakeDatagramIo& io() { return batch.io(); }
};

}  // namespace

TEST_F(FakeUdpEcho, SendsDatagramBackToItsSender) {
    // Given
    io().script_incoming({.payload = bytes("ping"), .peer = ipv6_peer(4000)});
    // When
    const auto echoed = batch.echo_once(-1);
    // Then
    ASSERT_TRUE(echoed.has_value()) << echoed.error().message();
    EXPECT_EQ(*echoed, 1U);
    ASSERT_EQ(io().sent().size(), 1U);
    EXPECT_EQ(io().sent().front().payload, bytes("ping"));
    EXPECT_TRUE(same_peer(io().sent().front().peer, ipv6_peer(4000)));
}

TEST_F(FakeUdpEcho, ReportsNothingEchoedWhenNoDatagramIsWaiting) {
    // Given
    // When
    const auto echoed = batch.echo_once(-1);
    // Then
    ASSERT_TRUE(echoed.has_value()) << echoed.error().message();
    EXPECT_EQ(*echoed, 0U);
    EXPECT_TRUE(io().sent().empty());
}

TEST_F(FakeUdpEcho, RetriesInterruptedReceive) {
    // Given
    io().script_receive_error(EINTR);
    io().script_incoming({.payload = bytes("ping"), .peer = ipv6_peer(4000)});
    // When
    const auto echoed = batch.echo_once(-1);
    // Then
    ASSERT_TRUE(echoed.has_value()) << echoed.error().message();
    EXPECT_EQ(*echoed, 1U);
}

TEST_F(FakeUdpEcho, ResendsRemainderAfterPartialSend) {
    // Given
    for (std::uint16_t port = 1; port <= 5; ++port) {
        io().script_incoming({.payload = bytes("d"), .peer = ipv6_peer(port)});
    }
    io().script_send({.max_messages = 3});
    // When
    const auto echoed = batch.echo_once(-1);
    // Then
    ASSERT_TRUE(echoed.has_value()) << echoed.error().message();
    EXPECT_EQ(*echoed, 5U);
    ASSERT_EQ(io().sent().size(), 5U);
    for (std::uint16_t port = 1; port <= 5; ++port) {
        EXPECT_TRUE(same_peer(io().sent()[port - 1U].peer, ipv6_peer(port)));
    }
}

TEST_F(FakeUdpEcho, SkipsDatagramThatCannotBeSent) {
    // Given
    io().script_incoming({.payload = bytes("bad"), .peer = ipv6_peer(1)});
    io().script_incoming({.payload = bytes("good"), .peer = ipv6_peer(2)});
    io().script_send({.error = EPERM});
    // When
    const auto echoed = batch.echo_once(-1);
    // Then
    ASSERT_TRUE(echoed.has_value()) << echoed.error().message();
    ASSERT_EQ(io().sent().size(), 1U);
    EXPECT_EQ(io().sent().front().payload, bytes("good"));
}

TEST_F(FakeUdpEcho, DropsBatchRemainderWhenSendBufferIsFull) {
    // Given
    io().script_incoming({.payload = bytes("a"), .peer = ipv6_peer(1)});
    io().script_incoming({.payload = bytes("b"), .peer = ipv6_peer(2)});
    io().script_send({.error = EAGAIN});
    // When
    const auto echoed = batch.echo_once(-1);
    // Then
    ASSERT_TRUE(echoed.has_value()) << echoed.error().message();
    EXPECT_TRUE(io().sent().empty());
    EXPECT_EQ(io().send_calls(), 1);
}
