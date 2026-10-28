// SPDX-License-Identifier: GPL-3.0-or-later
// Move-only RAII owner of a POSIX file descriptor.

module;

#include <unistd.h>

export module rfc862:fd;

export namespace rfc862 {

class FileDescriptor {
  public:
    explicit FileDescriptor(int fd) noexcept : fd_{fd} {}

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    FileDescriptor(FileDescriptor&&) = delete;
    FileDescriptor& operator=(FileDescriptor&&) = delete;

    ~FileDescriptor() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

  private:
    int fd_;
};

}  // namespace rfc862
