// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

import rfc862;

TEST(Module, IsImportable) {
    // Given
    constexpr bool imported = true;
    // When
    const bool result = imported;
    // Then
    EXPECT_TRUE(result);
}
