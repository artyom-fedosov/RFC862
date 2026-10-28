// SPDX-License-Identifier: GPL-3.0-or-later
// Thin wrappers over epoll(7) and eventfd(2).

module;

#include <cerrno>
#include <cstddef>
#include <span>
#include <utility>

#include <sys/epoll.h>

export module rfc862:epoll;

import :common;
import :fd;

export namespace rfc862 {

class Epoll {
  public:
    [[nodiscard]] static Result<Epoll> create() {
        FileDescriptor fd{::epoll_create1(EPOLL_CLOEXEC)};
        if (!fd) {
            return fail();
        }
        return Epoll{std::move(fd)};
    }

    // Waits into a caller-owned buffer so the event loop never allocates.
    [[nodiscard]] Result<std::span<const epoll_event>> wait(std::span<epoll_event> buffer,
                                                            int timeout_ms) const {
        for (;;) {
            const int count =
                ::epoll_wait(fd_.get(), buffer.data(), static_cast<int>(buffer.size()), timeout_ms);
            if (count >= 0) {
                return buffer.first(static_cast<std::size_t>(count));
            }
            if (errno != EINTR) {
                return fail();
            }
        }
    }

  private:
    explicit Epoll(FileDescriptor fd) noexcept : fd_{std::move(fd)} {}

    FileDescriptor fd_;
};

}  // namespace rfc862
