// SPDX-License-Identifier: GPL-3.0-or-later

#include <cerrno>
#include <utility>

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

TEST(FileDescriptor, IsInvalidWhenDefaultConstructed) {
    // Given
    // When
    const rfc862::FileDescriptor fd;
    // Then
    EXPECT_FALSE(fd.valid());
    EXPECT_FALSE(static_cast<bool>(fd));
    EXPECT_EQ(fd.get(), -1);
}

TEST(FileDescriptor, ExposesOwnedFd) {
    // Given
    const int raw = ::eventfd(0, EFD_CLOEXEC);
    ASSERT_GE(raw, 0);
    // When
    const rfc862::FileDescriptor fd{raw};
    // Then
    EXPECT_TRUE(fd.valid());
    EXPECT_TRUE(static_cast<bool>(fd));
    EXPECT_EQ(fd.get(), raw);
}

TEST(FileDescriptor, TransfersOwnershipOnMoveConstruction) {
    // Given
    const int raw = ::eventfd(0, EFD_CLOEXEC);
    ASSERT_GE(raw, 0);
    rfc862::FileDescriptor source{raw};
    // When
    const rfc862::FileDescriptor target{std::move(source)};
    // Then
    EXPECT_EQ(target.get(), raw);
    EXPECT_FALSE(source.valid());  // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    EXPECT_TRUE(is_open(raw));
}

TEST(FileDescriptor, ClosesPreviousFdOnMoveAssignment) {
    // Given
    const int first = ::eventfd(0, EFD_CLOEXEC);
    const int second = ::eventfd(0, EFD_CLOEXEC);
    ASSERT_GE(first, 0);
    ASSERT_GE(second, 0);
    rfc862::FileDescriptor target{first};
    rfc862::FileDescriptor source{second};
    // When
    target = std::move(source);
    // Then
    EXPECT_FALSE(is_open(first));
    EXPECT_EQ(target.get(), second);
    EXPECT_FALSE(source.valid());  // NOLINT(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
}
