// SPDX-License-Identifier: GPL-3.0-or-later
// Creation of the listening sockets the echo service needs.

module;

#include <array>
#include <cstdint>
#include <expected>

#include <netinet/in.h>
#include <sys/socket.h>

export module rfc862:socket;

import :common;
import :fd;

namespace rfc862 {

Result<void> set_option(int fd, int level, int name, int value) {
    if (::setsockopt(fd, level, name, &value, sizeof(value)) != 0) {
        return fail();
    }
    return {};
}

Result<FileDescriptor> make_bound_socket(int type, std::uint16_t port) {
    FileDescriptor fd{::socket(AF_INET6, type | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)};
    if (!fd) {
        return fail();
    }
    struct Option {
        int level;
        int name;
        int value;
    };
    // IPV6_V6ONLY=0 serves IPv4 and IPv6 from one socket regardless of the
    // net.ipv6.bindv6only sysctl. SO_REUSEPORT lets every worker thread bind its own
    // socket to the same port, so the kernel balances load between them.
    constexpr std::array options{
        Option{.level = IPPROTO_IPV6, .name = IPV6_V6ONLY, .value = 0},
        Option{.level = SOL_SOCKET, .name = SO_REUSEADDR, .value = 1},
        Option{.level = SOL_SOCKET, .name = SO_REUSEPORT, .value = 1},
    };
    for (const auto& [level, name, value] : options) {
        if (auto set = set_option(fd.get(), level, name, value); !set) {
            return std::unexpected{set.error()};
        }
    }
    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_any;
    address.sin6_port = htons(port);
    if (::bind(fd.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        return fail();
    }
    return fd;
}

}  // namespace rfc862

export namespace rfc862 {

[[nodiscard]] Result<FileDescriptor> make_tcp_listener(std::uint16_t port,
                                                       int backlog = SOMAXCONN) {
    auto fd = make_bound_socket(SOCK_STREAM, port);
    if (fd && ::listen(fd->get(), backlog) != 0) {
        return fail();
    }
    return fd;
}

[[nodiscard]] Result<FileDescriptor> make_udp_socket(std::uint16_t port) {
    return make_bound_socket(SOCK_DGRAM, port);
}

[[nodiscard]] Result<std::uint16_t> local_port(int fd) {
    sockaddr_in6 address{};
    socklen_t length = sizeof(address);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        return fail();
    }
    return ntohs(address.sin6_port);
}

}  // namespace rfc862
