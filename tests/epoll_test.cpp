// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>

#include <sys/epoll.h>

#include <gtest/gtest.h>

import rfc862;

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
