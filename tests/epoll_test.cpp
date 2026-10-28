// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstdint>

#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <gtest/gtest.h>

import rfc862;

namespace {

struct Pipe {
    rfc862::FileDescriptor read_end;
    rfc862::FileDescriptor write_end;
};

Pipe make_pipe() {
    std::array<int, 2> fds{-1, -1};
    if (::pipe2(fds.data(), O_CLOEXEC | O_NONBLOCK) != 0) {
        return {};
    }
    return {.read_end = rfc862::FileDescriptor{fds[0]},
            .write_end = rfc862::FileDescriptor{fds[1]}};
}

void write_byte(const rfc862::FileDescriptor& fd) {
    const char byte = 'x';
    ASSERT_EQ(::write(fd.get(), &byte, 1), 1);
}

}  // namespace

TEST(Epoll, ReturnsNoEventsOnTimeout) {
    // Given
    auto epoll = rfc862::Epoll::create();
    ASSERT_TRUE(epoll.has_value()) << epoll.error().message();
    std::array<epoll_event, 4> buffer{};
    // When
    const auto events = epoll->wait(buffer, 0);
    // Then
    ASSERT_TRUE(events.has_value()) << events.error().message();
    EXPECT_TRUE(events->empty());
}

TEST(Epoll, ReportsReadyFdWithItsTag) {
    // Given
    auto epoll = rfc862::Epoll::create();
    ASSERT_TRUE(epoll.has_value()) << epoll.error().message();
    const auto pipe = make_pipe();
    ASSERT_TRUE(pipe.read_end.valid());
    constexpr std::uint64_t tag = 42;
    ASSERT_TRUE(epoll->add(pipe.read_end.get(), EPOLLIN, tag).has_value());
    write_byte(pipe.write_end);
    std::array<epoll_event, 4> buffer{};
    // When
    const auto events = epoll->wait(buffer, 1000);
    // Then
    ASSERT_TRUE(events.has_value()) << events.error().message();
    ASSERT_EQ(events->size(), 1U);
    EXPECT_EQ((*events)[0].data.u64, tag);
    EXPECT_NE((*events)[0].events & EPOLLIN, 0U);
}

TEST(Epoll, ReportsNewInterestAfterModify) {
    // Given
    auto epoll = rfc862::Epoll::create();
    ASSERT_TRUE(epoll.has_value()) << epoll.error().message();
    const auto pipe = make_pipe();
    ASSERT_TRUE(pipe.write_end.valid());
    constexpr std::uint64_t tag = 7;
    ASSERT_TRUE(epoll->add(pipe.write_end.get(), 0, tag).has_value());
    std::array<epoll_event, 4> buffer{};
    // When
    const auto modified = epoll->modify(pipe.write_end.get(), EPOLLOUT, tag);
    // Then
    ASSERT_TRUE(modified.has_value()) << modified.error().message();
    const auto events = epoll->wait(buffer, 1000);
    ASSERT_TRUE(events.has_value());
    ASSERT_EQ(events->size(), 1U);
    EXPECT_NE(events->front().events & EPOLLOUT, 0U);
}

TEST(Epoll, StopsReportingRemovedFd) {
    // Given
    auto epoll = rfc862::Epoll::create();
    ASSERT_TRUE(epoll.has_value()) << epoll.error().message();
    const auto pipe = make_pipe();
    ASSERT_TRUE(pipe.read_end.valid());
    ASSERT_TRUE(epoll->add(pipe.read_end.get(), EPOLLIN, 1).has_value());
    write_byte(pipe.write_end);
    std::array<epoll_event, 4> buffer{};
    // When
    const auto removed = epoll->remove(pipe.read_end.get());
    // Then
    ASSERT_TRUE(removed.has_value()) << removed.error().message();
    const auto events = epoll->wait(buffer, 0);
    ASSERT_TRUE(events.has_value());
    EXPECT_TRUE(events->empty());
}

TEST(EventFd, WakesEpollWhenNotified) {
    // Given
    auto epoll = rfc862::Epoll::create();
    ASSERT_TRUE(epoll.has_value()) << epoll.error().message();
    auto event = rfc862::EventFd::create();
    ASSERT_TRUE(event.has_value()) << event.error().message();
    ASSERT_TRUE(epoll->add(event->fd(), EPOLLIN, 3).has_value());
    std::array<epoll_event, 4> buffer{};
    // When
    event->notify();
    // Then
    const auto events = epoll->wait(buffer, 1000);
    ASSERT_TRUE(events.has_value());
    ASSERT_EQ(events->size(), 1U);
    EXPECT_EQ(events->front().data.u64, 3U);
}
