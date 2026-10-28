// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <system_error>
#include <thread>
#include <vector>

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

std::vector<std::byte> pattern(std::size_t size, std::size_t seed = 0) {
    std::vector<std::byte> result(size);
    for (std::size_t i = 0; i < size; ++i) {
        result[i] = static_cast<std::byte>((i + seed) * 131 % 251);
    }
    return result;
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

TEST(Server, StreamsOneMebibyteBackUnchanged) {
    // Given
    auto server = rfc862::Server::start(test_options());
    ASSERT_TRUE(server.has_value()) << server.error().message();
    const auto client = rfc862::test::connect_tcp(server->tcp_port());
    ASSERT_TRUE(client.valid());
    const auto payload = pattern(1024UZ * 1024);
    std::vector<std::byte> echoed;
    // When
    std::thread reader{[&] { echoed = rfc862::test::receive_exactly(client, payload.size()); }};
    const bool sent = rfc862::test::send_all(client, payload);
    reader.join();
    // Then
    EXPECT_TRUE(sent);
    EXPECT_EQ(echoed, payload);
}

TEST(Server, ServesManyConcurrentClients) {
    // Given
    auto server = rfc862::Server::start(test_options());
    ASSERT_TRUE(server.has_value()) << server.error().message();
    constexpr std::size_t clients = 100;
    std::atomic<std::size_t> correct{0};
    // When
    {
        std::vector<std::jthread> threads;
        threads.reserve(clients);
        for (std::size_t i = 0; i < clients; ++i) {
            threads.emplace_back([&, i, port = server->tcp_port()] {
                const auto client = rfc862::test::connect_tcp(port);
                const auto payload = pattern(64UZ * 1024, i);
                std::vector<std::byte> echoed;
                std::thread reader{
                    [&] { echoed = rfc862::test::receive_exactly(client, payload.size()); }};
                const bool sent = rfc862::test::send_all(client, payload);
                reader.join();
                if (sent && echoed == payload) {
                    ++correct;
                }
            });
        }
    }
    // Then
    EXPECT_EQ(correct.load(), clients);
}

TEST(Server, EchoesToIpv6Clients) {
    // Given
    auto server = rfc862::Server::start(test_options());
    ASSERT_TRUE(server.has_value()) << server.error().message();
    const auto client = rfc862::test::connect_tcp(server->tcp_port(), rfc862::test::Family::V6);
    ASSERT_TRUE(client.valid());
    // When
    ASSERT_TRUE(rfc862::test::send_all(client, bytes("over ipv6")));
    // Then
    EXPECT_EQ(rfc862::test::receive_exactly(client, 9), bytes("over ipv6"));
}
