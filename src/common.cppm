// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions shared by all partitions.

module;

#include <cstdint>

export module rfc862:common;

export namespace rfc862 {

// RFC 862: "A server listens for TCP connections on TCP port 7" and "on UDP port 7".
inline constexpr std::uint16_t echo_port = 7;

}  // namespace rfc862
