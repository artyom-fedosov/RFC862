// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <system_error>

#include <netinet/in.h>
#include <sys/socket.h>

#include <gtest/gtest.h>

import rfc862;
import rfc862.test.clients;
import rfc862.test.fake_stream_io;

namespace {

using rfc862::test::bytes;

rfc862::ServerOptions test_options() {
    return {.port = 0, .workers = 2};
}

std::size_t open_descriptor_count() {
    const std::filesystem::directory_iterator entries{"/proc/self/fd"};
    return static_cast<std::size_t>(std::distance(begin(entries), end(entries)));
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

TEST(Server, StopsPromptlyAndClosesConnectedClientsCleanly) {
    // Given
    auto server = rfc862::Server::start(test_options());
    ASSERT_TRUE(server.has_value()) << server.error().message();
    const auto client = rfc862::test::connect_tcp(server->tcp_port());
    ASSERT_TRUE(client.valid());
    ASSERT_TRUE(rfc862::test::send_all(client, bytes("x")));
    ASSERT_EQ(rfc862::test::receive_exactly(client, 1), bytes("x"));
    const auto started = std::chrono::steady_clock::now();
    // When
    server->stop();
    server->wait();
    // Then
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds{1});
    EXPECT_TRUE(rfc862::test::receives_eof(client));
}

TEST(Server, ReleasesEveryDescriptorWhenDestroyed) {
    // Given
    const auto before = open_descriptor_count();
    // When
    {
        auto server = rfc862::Server::start(test_options());
        ASSERT_TRUE(server.has_value()) << server.error().message();
        const auto client = rfc862::test::connect_tcp(server->tcp_port());
        ASSERT_TRUE(rfc862::test::send_all(client, bytes("x")));
        ASSERT_EQ(rfc862::test::receive_exactly(client, 1), bytes("x"));
    }
    // Then
    EXPECT_EQ(open_descriptor_count(), before);
}

TEST(Server, ReportsBindFailureBeforeStarting) {
    // Given
    const rfc862::FileDescriptor blocker{::socket(AF_INET6, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_any;
    ASSERT_EQ(::bind(blocker.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)),
              0);
    ASSERT_EQ(::listen(blocker.get(), 1), 0);
    const auto taken = rfc862::local_port(blocker.get());
    ASSERT_TRUE(taken.has_value());
    // When
    const auto server = rfc862::Server::start({.port = *taken, .workers = 2});
    // Then
    ASSERT_FALSE(server.has_value());
    EXPECT_EQ(server.error(), std::errc::address_in_use);
}
