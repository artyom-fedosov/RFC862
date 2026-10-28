// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>

#include <gtest/gtest.h>

import rfc862;

TEST(EchoPort, IsSevenAsDefinedByRfc862) {
    // Given
    // When
    constexpr auto port = rfc862::echo_port;
    // Then
    EXPECT_EQ(port, 7);
}

TEST(TcpListener, BindsToEphemeralPort) {
    // Given
    constexpr std::uint16_t any_port = 0;
    // When
    const auto listener = rfc862::make_tcp_listener(any_port);
    // Then
    ASSERT_TRUE(listener.has_value()) << listener.error().message();
    const auto port = rfc862::local_port(listener->get());
    ASSERT_TRUE(port.has_value()) << port.error().message();
    EXPECT_GT(*port, 0);
}
