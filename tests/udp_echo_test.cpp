// SPDX-License-Identifier: GPL-3.0-or-later

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
