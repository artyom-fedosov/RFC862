// SPDX-License-Identifier: GPL-3.0-or-later

#include <cerrno>

#include <fcntl.h>
#include <sys/eventfd.h>

#include <gtest/gtest.h>

import rfc862;

namespace {

bool is_open(int fd) {
    return ::fcntl(fd, F_GETFD) != -1 || errno != EBADF;
}

}  // namespace

TEST(FileDescriptor, ClosesFdOnDestruction) {
    // Given
    const int raw = ::eventfd(0, EFD_CLOEXEC);
    ASSERT_GE(raw, 0);
    // When
    {
        const rfc862::FileDescriptor fd{raw};
    }
    // Then
    EXPECT_FALSE(is_open(raw));
}
