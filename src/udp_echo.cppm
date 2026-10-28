// SPDX-License-Identifier: GPL-3.0-or-later
// Batched UDP echo (RFC 862: "When a datagram is received, the data from it is sent
// back in an answering datagram").

module;

#include <array>
#include <cerrno>
#include <concepts>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>

#include <netinet/in.h>
#include <sys/socket.h>

export module rfc862:udp_echo;

import :common;

export namespace rfc862 {

// I/O seam with recvmmsg(2)/sendmmsg(2) semantics: a negative result reports errno.
template <class Io>
concept DatagramIo = requires(Io& io, int fd, std::span<mmsghdr> messages) {
    { io.recvmmsg(fd, messages) } -> std::same_as<int>;
    { io.sendmmsg(fd, messages) } -> std::same_as<int>;
};

struct SystemDatagramIo {
    static int recvmmsg(int fd, std::span<mmsghdr> messages) noexcept {
        return ::recvmmsg(fd, messages.data(), static_cast<unsigned>(messages.size()), MSG_DONTWAIT,
                          nullptr);
    }
    static int sendmmsg(int fd, std::span<mmsghdr> messages) noexcept {
        return ::sendmmsg(fd, messages.data(), static_cast<unsigned>(messages.size()),
                          MSG_DONTWAIT);
    }
};

// Datagrams moved per system call.
inline constexpr std::size_t udp_batch_size = 64;
// Largest UDP payload over IPv4 or IPv6 (without jumbograms) fits in 64 KiB - 1.
inline constexpr std::size_t max_datagram_size = 65535;

// Owns all buffers for one batch up front, so echoing never allocates. Each received
// datagram is sent back from the very buffer it arrived in: no copies at all.
template <DatagramIo Io = SystemDatagramIo>
class BasicUdpEchoBatch {
  public:
    explicit BasicUdpEchoBatch(Io io = {})
        : io_{std::move(io)},
          storage_{
              std::make_unique_for_overwrite<std::byte[]>(udp_batch_size * max_datagram_size)} {}

    // Echoes one batch. Returns how many datagrams were received (0 when none were
    // waiting).
    [[nodiscard]] Result<std::size_t> echo_once(int fd) {
        prepare_for_receive();
        int received = 0;
        while ((received = io_.recvmmsg(fd, messages_)) < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;
            }
            if (errno != EINTR) {
                return fail();
            }
        }
        const auto count = static_cast<std::size_t>(received);
        for (std::size_t i = 0; i < count; ++i) {
            iovecs_[i].iov_len = messages_[i].msg_len;
        }
        send_all(fd, std::span{messages_}.first(count));
        return count;
    }

    [[nodiscard]] Io& io() noexcept { return io_; }

  private:
    void send_all(int fd, std::span<mmsghdr> pending) {
        while (!pending.empty()) {
            const int sent = io_.sendmmsg(fd, pending);
            if (sent > 0) {
                pending = pending.subspan(static_cast<std::size_t>(sent));
            } else if (sent < 0 && errno == EINTR) {
                continue;
            } else if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                // UDP is lossy by design: never stall the worker on a full send buffer.
                break;
            } else {
                // sendmmsg reports an error only for the first datagram (e.g. EPERM from a
                // firewall); skip just that one.
                pending = pending.subspan(1);
            }
        }
    }

    void prepare_for_receive() noexcept {
        for (std::size_t i = 0; i < udp_batch_size; ++i) {
            iovecs_[i] = {.iov_base = storage_.get() + (i * max_datagram_size),
                          .iov_len = max_datagram_size};
            messages_[i] = {};
            messages_[i].msg_hdr.msg_name = &peers_[i];
            messages_[i].msg_hdr.msg_namelen = sizeof(peers_[i]);
            messages_[i].msg_hdr.msg_iov = &iovecs_[i];
            messages_[i].msg_hdr.msg_iovlen = 1;
        }
    }

    [[no_unique_address]] Io io_;
    std::unique_ptr<std::byte[]> storage_;
    std::array<mmsghdr, udp_batch_size> messages_{};
    std::array<iovec, udp_batch_size> iovecs_{};
    std::array<sockaddr_in6, udp_batch_size> peers_{};
};

using UdpEchoBatch = BasicUdpEchoBatch<>;

}  // namespace rfc862
