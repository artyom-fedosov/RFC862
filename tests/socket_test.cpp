// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>

#include <fcntl.h>

#include <gtest/gtest.h>

import rfc862;

namespace {

bool is_nonblocking(int fd) {
    return (::fcntl(fd, F_GETFL) & O_NONBLOCK) != 0;
}

bool is_close_on_exec(int fd) {
    return (::fcntl(fd, F_GETFD) & FD_CLOEXEC) != 0;
}

}  // namespace

TEST(EchoPort, IsSevenAsDefinedByRfc862) {
    // Given
    // When
    constexpr auto port = rfc862::echo_port;
    // Then
    EXPECT_EQ(port, 7);
}

TEST(TcpListener, BindsToEphemeralPort) {
    // Given
    constexpr std::uint16_t any_port = 0;
    // When
    const auto listener = rfc862::make_tcp_listener(any_port);
    // Then
    ASSERT_TRUE(listener.has_value()) << listener.error().message();
    const auto port = rfc862::local_port(listener->get());
    ASSERT_TRUE(port.has_value()) << port.error().message();
    EXPECT_GT(*port, 0);
}

TEST(UdpSocket, BindsToEphemeralPort) {
    // Given
    constexpr std::uint16_t any_port = 0;
    // When
    const auto socket = rfc862::make_udp_socket(any_port);
    // Then
    ASSERT_TRUE(socket.has_value()) << socket.error().message();
    const auto port = rfc862::local_port(socket->get());
    ASSERT_TRUE(port.has_value()) << port.error().message();
    EXPECT_GT(*port, 0);
}

TEST(TcpListener, IsNonBlockingAndCloseOnExec) {
    // Given
    constexpr std::uint16_t any_port = 0;
    // When
    const auto listener = rfc862::make_tcp_listener(any_port);
    // Then
    ASSERT_TRUE(listener.has_value()) << listener.error().message();
    EXPECT_TRUE(is_nonblocking(listener->get()));
    EXPECT_TRUE(is_close_on_exec(listener->get()));
}

TEST(UdpSocket, IsNonBlockingAndCloseOnExec) {
    // Given
    constexpr std::uint16_t any_port = 0;
    // When
    const auto socket = rfc862::make_udp_socket(any_port);
    // Then
    ASSERT_TRUE(socket.has_value()) << socket.error().message();
    EXPECT_TRUE(is_nonblocking(socket->get()));
    EXPECT_TRUE(is_close_on_exec(socket->get()));
}
