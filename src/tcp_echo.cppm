// SPDX-License-Identifier: GPL-3.0-or-later
// Per-connection TCP echo (RFC 862: "Once a connection is established any data
// received is sent back"), as a state machine over a non-blocking stream.

module;

#include <cerrno>

#include <sys/types.h>
#include <unistd.h>

export module rfc862:tcp_echo;

import std;
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

// Reads served per readiness event before yielding to other connections. Connections
// are polled level-triggered, so leftover input is reported again on the next round.
inline constexpr std::size_t max_reads_per_event = 16;

// What the connection waits for next.
enum class Progress : std::uint8_t { WantRead, WantWrite, Closed };

template <StreamIo Io = SystemStreamIo>
class BasicTcpEchoConnection {
  public:
    explicit BasicTcpEchoConnection(FileDescriptor fd, Io io = {}) noexcept
        : fd_{std::move(fd)}, io_{std::move(io)} {}

    // Reads into the worker's shared scratch buffer and writes it straight back. Only
    // bytes the socket would not take are copied into the connection's own buffer;
    // reading then pauses until they drain (backpressure).
    [[nodiscard]] Progress on_readable(std::span<std::byte> scratch) {
        for (std::size_t reads = 0; reads < max_reads_per_event; ++reads) {
            const ssize_t received = io_.read(fd_.get(), scratch);
            if (received > 0) {
                const auto data = scratch.first(static_cast<std::size_t>(received));
                const auto written = write_some(data);
                if (!written) {
                    return Progress::Closed;
                }
                if (*written < data.size()) {
                    // std::vector::assign compiles to memcpy, which glibc vectorises
                    // (AVX2/AVX-512/NEON/SVE) and dispatches at runtime.
                    const auto rest = data.subspan(*written);
                    pending_.assign(rest.begin(), rest.end());
                    pending_offset_ = 0;
                    return Progress::WantWrite;
                }
                continue;
            }
            if (received == 0) {
                // RFC 862: the connection lasts until the user terminates it. Reading
                // pauses while bytes are pending, so everything has been echoed by now.
                return Progress::Closed;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return Progress::WantRead;
            }
            return Progress::Closed;
        }
        return Progress::WantRead;
    }

    // Continues sending the bytes left over from a short write.
    [[nodiscard]] Progress on_writable() {
        const auto rest = std::span{pending_}.subspan(pending_offset_);
        const auto written = write_some(rest);
        if (!written) {
            return Progress::Closed;
        }
        pending_offset_ += *written;
        if (pending_offset_ < pending_.size()) {
            return Progress::WantWrite;
        }
        pending_.clear();
        pending_offset_ = 0;
        return Progress::WantRead;
    }

    [[nodiscard]] std::size_t pending_bytes() const noexcept {
        return pending_.size() - pending_offset_;
    }
    [[nodiscard]] int fd() const noexcept { return fd_.get(); }
    [[nodiscard]] Io& io() noexcept { return io_; }

  private:
    // Writes as much of data as the socket accepts without blocking. Returns the
    // number of bytes written, or nothing if the connection failed.
    std::optional<std::size_t> write_some(std::span<const std::byte> data) {
        std::size_t written = 0;
        while (written < data.size()) {
            const ssize_t sent = io_.write(fd_.get(), data.subspan(written));
            if (sent > 0) {
                written += static_cast<std::size_t>(sent);
            } else if (sent < 0 && errno == EINTR) {
                continue;
            } else if (sent == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else {
                return std::nullopt;
            }
        }
        return written;
    }

    FileDescriptor fd_;
    std::vector<std::byte> pending_;
    std::size_t pending_offset_ = 0;
    [[no_unique_address]] Io io_;
};

using TcpEchoConnection = BasicTcpEchoConnection<>;

}  // namespace rfc862
