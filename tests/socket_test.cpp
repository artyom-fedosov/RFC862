// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

import rfc862;

TEST(EchoPort, IsSevenAsDefinedByRfc862) {
    // Given
    // When
    constexpr auto port = rfc862::echo_port;
    // Then
    EXPECT_EQ(port, 7);
}
