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
    FileDescriptor(FileDescriptor&& other) noexcept : fd_{std::exchange(other.fd_, -1)} {}

    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    ~FileDescriptor() { close(); }

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
    explicit operator bool() const noexcept { return valid(); }

  private:
    void close() noexcept {
        if (valid()) {
            ::close(std::exchange(fd_, -1));
        }
    }

    int fd_{-1};
};

}  // namespace rfc862
