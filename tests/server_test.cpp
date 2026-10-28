// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

import rfc862;
import rfc862.test.clients;
import rfc862.test.fake_stream_io;

namespace {

using rfc862::test::bytes;

rfc862::ServerOptions test_options() {
    return {.port = 0, .workers = 2};
}

}  // namespace

TEST(Server, EchoesOverTcp) {
    // Given
    auto server = rfc862::Server::start(test_options());
    ASSERT_TRUE(server.has_value()) << server.error().message();
    const auto client = rfc862::test::connect_tcp(server->tcp_port());
    ASSERT_TRUE(client.valid());
    // When
    ASSERT_TRUE(rfc862::test::send_all(client, bytes("hello server")));
    // Then
    EXPECT_EQ(rfc862::test::receive_exactly(client, 12), bytes("hello server"));
}

TEST(Server, EchoesOverUdp) {
    // Given
    auto server = rfc862::Server::start(test_options());
    ASSERT_TRUE(server.has_value()) << server.error().message();
    const auto client = rfc862::test::connect_udp(server->udp_port());
    ASSERT_TRUE(client.valid());
    // When
    ASSERT_TRUE(rfc862::test::send_all(client, bytes("datagram")));
    // Then
    EXPECT_EQ(rfc862::test::receive_exactly(client, 8), bytes("datagram"));
}
