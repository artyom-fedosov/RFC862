// SPDX-License-Identifier: GPL-3.0-or-later

#include <cerrno>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

import std;
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

TEST_F(FakeUdpEcho, KeepsReceivingWhileBatchesAreFull) {
    // Given
    const std::size_t total = rfc862::udp_batch_size + 10;
    for (std::size_t i = 0; i < total; ++i) {
        io().script_incoming({.payload = bytes("x"), .peer = ipv6_peer(1)});
    }
    // When
    const auto echoed = batch.echo_all(-1);
    // Then
    EXPECT_EQ(echoed, total);
    EXPECT_EQ(io().receive_calls(), 2);
}

TEST_F(FakeUdpEcho, YieldsAfterBatchLimitSoTcpClientsAreServed) {
    // Given
    const std::size_t total = rfc862::udp_batch_size * rfc862::max_batches_per_event * 2;
    for (std::size_t i = 0; i < total; ++i) {
        io().script_incoming({.payload = bytes("x"), .peer = ipv6_peer(1)});
    }
    // When
    const auto echoed = batch.echo_all(-1);
    // Then
    EXPECT_EQ(echoed, rfc862::udp_batch_size * rfc862::max_batches_per_event);
}

namespace {

class LoopbackUdpEcho : public ::testing::Test {
  protected:
    void SetUp() override {
        auto socket = rfc862::make_udp_socket(0);
        ASSERT_TRUE(socket.has_value()) << socket.error().message();
        server = std::move(*socket);
        const auto port = rfc862::local_port(server.get());
        ASSERT_TRUE(port.has_value());
        server_address.sin_family = AF_INET;
        server_address.sin_port = htons(*port);
        server_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    }

    [[nodiscard]] static rfc862::FileDescriptor make_client() {
        rfc862::FileDescriptor client{::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0)};
        const timeval timeout{.tv_sec = 2, .tv_usec = 0};
        ::setsockopt(client.get(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        const int buffer = 4 * 1024 * 1024;
        ::setsockopt(client.get(), SOL_SOCKET, SO_RCVBUF, &buffer, sizeof(buffer));
        return client;
    }

    void send_from(const rfc862::FileDescriptor& client, const std::vector<std::byte>& data) const {
        ASSERT_EQ(
            ::sendto(client.get(), data.data(), data.size(), 0,
                     reinterpret_cast<const sockaddr*>(&server_address), sizeof(server_address)),
            static_cast<ssize_t>(data.size()));
    }

    [[nodiscard]] static std::vector<std::byte> receive(const rfc862::FileDescriptor& client) {
        std::vector<std::byte> buffer(rfc862::max_datagram_size);
        const ssize_t size = ::recv(client.get(), buffer.data(), buffer.size(), 0);
        buffer.resize(size < 0 ? 0 : static_cast<std::size_t>(size));
        return buffer;
    }

    rfc862::FileDescriptor server;
    sockaddr_in server_address{};
    rfc862::UdpEchoBatch batch;
};

}  // namespace

TEST_F(LoopbackUdpEcho, EchoesDatagramToSender) {
    // Given
    const auto client = make_client();
    send_from(client, bytes("hello"));
    // When
    const auto echoed = batch.echo_all(server.get());
    // Then
    EXPECT_EQ(echoed, 1U);
    EXPECT_EQ(receive(client), bytes("hello"));
}

TEST_F(LoopbackUdpEcho, AnswersEachSenderSeparately) {
    // Given
    const auto alice = make_client();
    const auto bob = make_client();
    send_from(alice, bytes("from alice"));
    send_from(bob, bytes("from bob"));
    // When
    const auto echoed = batch.echo_all(server.get());
    // Then
    EXPECT_EQ(echoed, 2U);
    EXPECT_EQ(receive(alice), bytes("from alice"));
    EXPECT_EQ(receive(bob), bytes("from bob"));
}

TEST_F(LoopbackUdpEcho, EchoesManyDatagramsAcrossBatches) {
    // Given
    const auto client = make_client();
    constexpr std::size_t count = 200;
    for (std::size_t i = 0; i < count; ++i) {
        send_from(client, {static_cast<std::byte>(i)});
    }
    // When
    const auto echoed = batch.echo_all(server.get());
    // Then
    EXPECT_EQ(echoed, count);
    for (std::size_t i = 0; i < count; ++i) {
        EXPECT_EQ(receive(client), std::vector{static_cast<std::byte>(i)});
    }
}

TEST_F(LoopbackUdpEcho, EchoesEmptyDatagram) {
    // Given
    const auto client = make_client();
    send_from(client, {});
    // When
    const auto echoed = batch.echo_all(server.get());
    // Then
    EXPECT_EQ(echoed, 1U);
    std::vector<std::byte> buffer(16);
    EXPECT_EQ(::recv(client.get(), buffer.data(), buffer.size(), 0), 0);
}

TEST_F(LoopbackUdpEcho, EchoesLargestIpv4Datagram) {
    // Given
    const auto client = make_client();
    std::vector<std::byte> payload(65507);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<std::byte>(i % 253);
    }
    send_from(client, payload);
    // When
    const auto echoed = batch.echo_all(server.get());
    // Then
    EXPECT_EQ(echoed, 1U);
    EXPECT_EQ(receive(client), payload);
}
