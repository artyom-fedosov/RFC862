// SPDX-License-Identifier: GPL-3.0-or-later
// Thin wrappers over epoll(7) and eventfd(2).

module;

#include <cerrno>

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

export module rfc862:epoll;

import std;
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

    // The tag comes back in epoll_event::data.u64 so callers can dispatch without a lookup.
    [[nodiscard]] Result<void> add(int fd, std::uint32_t events, std::uint64_t tag) const {
        return control(EPOLL_CTL_ADD, fd, events, tag);
    }

    [[nodiscard]] Result<void> modify(int fd, std::uint32_t events, std::uint64_t tag) const {
        return control(EPOLL_CTL_MOD, fd, events, tag);
    }

    [[nodiscard]] Result<void> remove(int fd) const { return control(EPOLL_CTL_DEL, fd, 0, 0); }

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
    [[nodiscard]] Result<void> control(int operation, int fd, std::uint32_t events,
                                       std::uint64_t tag) const {
        epoll_event event{.events = events, .data = {.u64 = tag}};
        if (::epoll_ctl(fd_.get(), operation, fd, &event) != 0) {
            return fail();
        }
        return {};
    }

    explicit Epoll(FileDescriptor fd) noexcept : fd_{std::move(fd)} {}

    FileDescriptor fd_;
};

class EventFd {
  public:
    [[nodiscard]] static Result<EventFd> create() {
        FileDescriptor fd{::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)};
        if (!fd) {
            return fail();
        }
        return EventFd{std::move(fd)};
    }

    // Can only fail with EAGAIN when the counter would overflow, which means it is
    // already signalled, so the result is deliberately ignored.
    void notify() const noexcept {
        const std::uint64_t one = 1;
        [[maybe_unused]] const auto written = ::write(fd_.get(), &one, sizeof(one));
    }

    // Resets the counter to zero. Fails only with EAGAIN when it is already zero.
    void drain() const noexcept {
        std::uint64_t value = 0;
        [[maybe_unused]] const auto read = ::read(fd_.get(), &value, sizeof(value));
    }

    [[nodiscard]] int fd() const noexcept { return fd_.get(); }

  private:
    explicit EventFd(FileDescriptor fd) noexcept : fd_{std::move(fd)} {}

    FileDescriptor fd_;
};

}  // namespace rfc862
