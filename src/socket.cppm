// SPDX-License-Identifier: GPL-3.0-or-later
// Creation of the listening sockets the echo service needs.

module;

#include <cstdint>

#include <netinet/in.h>
#include <sys/socket.h>

export module rfc862:socket;

import :common;
import :fd;

namespace rfc862 {

Result<FileDescriptor> make_bound_socket(int type, std::uint16_t port) {
    FileDescriptor fd{::socket(AF_INET6, type, 0)};
    if (!fd) {
        return fail();
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

[[nodiscard]] Result<std::uint16_t> local_port(int fd) {
    sockaddr_in6 address{};
    socklen_t length = sizeof(address);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        return fail();
    }
    return ntohs(address.sin6_port);
}

}  // namespace rfc862
