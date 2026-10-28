// SPDX-License-Identifier: GPL-3.0-or-later
// Per-connection TCP echo (RFC 862: "Once a connection is established any data
// received is sent back"), as a state machine over a non-blocking stream.

module;

#include <cerrno>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include <sys/types.h>
#include <unistd.h>

export module rfc862:tcp_echo;

import :fd;

export namespace rfc862 {

// I/O seam with read(2)/write(2) semantics: a negative result reports errno.
template <class Io>
concept StreamIo =
    requires(Io& io, int fd, std::span<std::byte> in, std::span<const std::byte> out) {
        { io.read(fd, in) } -> std::same_as<ssize_t>;
        { io.write(fd, out) } -> std::same_as<ssize_t>;
    };

struct SystemStreamIo {
    static ssize_t read(int fd, std::span<std::byte> buffer) noexcept {
        return ::read(fd, buffer.data(), buffer.size());
    }
    static ssize_t write(int fd, std::span<const std::byte> data) noexcept {
        return ::write(fd, data.data(), data.size());
    }
};

// What the connection waits for next.
enum class Progress : std::uint8_t { WantRead, WantWrite, Closed };

template <StreamIo Io = SystemStreamIo>
class BasicTcpEchoConnection {
  public:
    explicit BasicTcpEchoConnection(FileDescriptor fd, Io io = {}) noexcept
        : fd_{std::move(fd)}, io_{std::move(io)} {}

    // Reads into the worker's shared scratch buffer and writes it straight back.
    [[nodiscard]] Progress on_readable(std::span<std::byte> scratch) {
        for (;;) {
            const ssize_t received = io_.read(fd_.get(), scratch);
            if (received > 0) {
                io_.write(fd_.get(), scratch.first(static_cast<std::size_t>(received)));
                continue;
            }
            return Progress::WantRead;
        }
    }

    [[nodiscard]] std::size_t pending_bytes() const noexcept { return 0; }
    [[nodiscard]] int fd() const noexcept { return fd_.get(); }
    [[nodiscard]] Io& io() noexcept { return io_; }

  private:
    FileDescriptor fd_;
    [[no_unique_address]] Io io_;
};

using TcpEchoConnection = BasicTcpEchoConnection<>;

}  // namespace rfc862
