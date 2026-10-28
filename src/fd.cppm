// SPDX-License-Identifier: GPL-3.0-or-later
// Move-only RAII owner of a POSIX file descriptor.

module;

#include <utility>

#include <unistd.h>

export module rfc862:fd;

export namespace rfc862 {

class FileDescriptor {
  public:
    FileDescriptor() noexcept = default;
    explicit FileDescriptor(int fd) noexcept : fd_{fd} {}

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    FileDescriptor(FileDescriptor&& other) noexcept : fd_{other.release()} {}

    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    ~FileDescriptor() { reset(); }

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
    explicit operator bool() const noexcept { return valid(); }

    [[nodiscard]] int release() noexcept { return std::exchange(fd_, -1); }

    void reset(int fd = -1) noexcept {
        if (const int old = std::exchange(fd_, fd); old >= 0) {
            ::close(old);
        }
    }

    friend void swap(FileDescriptor& a, FileDescriptor& b) noexcept { std::swap(a.fd_, b.fd_); }

  private:
    int fd_{-1};
};

static_assert(sizeof(FileDescriptor) == sizeof(int));

}  // namespace rfc862
