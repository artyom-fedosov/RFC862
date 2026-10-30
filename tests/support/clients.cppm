// SPDX-License-Identifier: GPL-3.0-or-later
// Blocking loopback clients for integration tests.

module;

#include <cerrno>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

export module rfc862.test.clients;

import std;
import rfc862;

namespace rfc862::test {

void set_timeout(int fd) {
    const timeval timeout{.tv_sec = 5, .tv_usec = 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

}  // namespace rfc862::test

export namespace rfc862::test {

enum class Family : std::uint8_t { V4, V6 };

// Connects a blocking TCP client (5 s timeouts) to the loopback address. Returns an
// invalid descriptor on failure.
[[nodiscard]] FileDescriptor connect_tcp(std::uint16_t port, Family family = Family::V4) {
    FileDescriptor fd{
        ::socket(family == Family::V4 ? AF_INET : AF_INET6, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    if (!fd) {
        return {};
    }
    set_timeout(fd.get());
    int result = -1;
    if (family == Family::V4) {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        result = ::connect(fd.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    } else {
        sockaddr_in6 address{};
        address.sin6_family = AF_INET6;
        address.sin6_port = htons(port);
        address.sin6_addr = in6addr_loopback;
        result = ::connect(fd.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    }
    if (result != 0) {
        return {};
    }
    return fd;
}

// Connects a blocking UDP client (5 s timeouts) to the IPv4 loopback address. Returns
// an invalid descriptor on failure.
[[nodiscard]] FileDescriptor connect_udp(std::uint16_t port) {
    FileDescriptor fd{::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0)};
    if (!fd) {
        return {};
    }
    set_timeout(fd.get());
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        return {};
    }
    return fd;
}

[[nodiscard]] bool send_all(const FileDescriptor& fd, std::span<const std::byte> data) {
    while (!data.empty()) {
        const ssize_t sent = ::send(fd.get(), data.data(), data.size(), MSG_NOSIGNAL);
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (sent <= 0) {
            return false;
        }
        data = data.subspan(static_cast<std::size_t>(sent));
    }
    return true;
}

// Receives exactly size bytes, or fewer if the peer closes or the timeout expires.
[[nodiscard]] std::vector<std::byte> receive_exactly(const FileDescriptor& fd, std::size_t size) {
    std::vector<std::byte> result(size);
    std::size_t received = 0;
    while (received < size) {
        const ssize_t count = ::recv(fd.get(), result.data() + received, size - received, 0);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            break;
        }
        received += static_cast<std::size_t>(count);
    }
    result.resize(received);
    return result;
}

// True when the peer closed the connection in an orderly way (FIN, not RST).
[[nodiscard]] bool receives_eof(const FileDescriptor& fd) {
    std::byte byte{};
    return ::recv(fd.get(), &byte, 1, 0) == 0;
}

}  // namespace rfc862::test
