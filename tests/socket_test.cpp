// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <gtest/gtest.h>

import rfc862;

namespace {

bool is_nonblocking(int fd) {
    return (::fcntl(fd, F_GETFL) & O_NONBLOCK) != 0;
}

bool is_close_on_exec(int fd) {
    return (::fcntl(fd, F_GETFD) & FD_CLOEXEC) != 0;
}

int ipv6_only(int fd) {
    int value = -1;
    socklen_t length = sizeof(value);
    ::getsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &value, &length);
    return value;
}

bool connect_ipv4_loopback(int fd, std::uint16_t port) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return ::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
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

TEST(TcpListener, AcceptsIpv4Clients) {
    // Given
    const auto listener = rfc862::make_tcp_listener(0);
    ASSERT_TRUE(listener.has_value()) << listener.error().message();
    const auto port = rfc862::local_port(listener->get());
    ASSERT_TRUE(port.has_value());
    const rfc862::FileDescriptor client{::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    // When
    const bool connected = connect_ipv4_loopback(client.get(), *port);
    // Then
    EXPECT_TRUE(connected);
    EXPECT_EQ(ipv6_only(listener->get()), 0);
}

TEST(UdpSocket, IsDualStack) {
    // Given
    // When
    const auto socket = rfc862::make_udp_socket(0);
    // Then
    ASSERT_TRUE(socket.has_value()) << socket.error().message();
    EXPECT_EQ(ipv6_only(socket->get()), 0);
}

TEST(TcpListener, SharesPortWithAnotherListener) {
    // Given
    const auto first = rfc862::make_tcp_listener(0);
    ASSERT_TRUE(first.has_value()) << first.error().message();
    const auto port = rfc862::local_port(first->get());
    ASSERT_TRUE(port.has_value());
    // When
    const auto second = rfc862::make_tcp_listener(*port);
    // Then
    EXPECT_TRUE(second.has_value()) << second.error().message();
}

TEST(UdpSocket, SharesPortWithAnotherSocket) {
    // Given
    const auto first = rfc862::make_udp_socket(0);
    ASSERT_TRUE(first.has_value()) << first.error().message();
    const auto port = rfc862::local_port(first->get());
    ASSERT_TRUE(port.has_value());
    // When
    const auto second = rfc862::make_udp_socket(*port);
    // Then
    EXPECT_TRUE(second.has_value()) << second.error().message();
}
