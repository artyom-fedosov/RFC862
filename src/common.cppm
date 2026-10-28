// SPDX-License-Identifier: GPL-3.0-or-later
// Definitions shared by all partitions.

module;

#include <cerrno>
#include <cstdint>
#include <expected>
#include <system_error>

export module rfc862:common;

export namespace rfc862 {

// RFC 862: "A server listens for TCP connections on TCP port 7" and "on UDP port 7".
inline constexpr std::uint16_t echo_port = 7;

template <class T>
using Result = std::expected<T, std::error_code>;

[[nodiscard]] inline std::error_code last_error() noexcept {
    return {errno, std::system_category()};
}

[[nodiscard]] inline std::unexpected<std::error_code> fail() noexcept {
    return std::unexpected{last_error()};
}

}  // namespace rfc862
