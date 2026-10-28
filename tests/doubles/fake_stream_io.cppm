// SPDX-License-Identifier: GPL-3.0-or-later
// Test double for rfc862::StreamIo: scripted reads and writes (fake) that also
// records everything written and every call made (spy).

module;

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <deque>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <sys/types.h>

export module rfc862.test.fake_stream_io;

export namespace rfc862::test {

[[nodiscard]] inline std::vector<std::byte> bytes(std::string_view text) {
    std::vector<std::byte> result(text.size());
    std::ranges::transform(text, result.begin(), [](char c) { return static_cast<std::byte>(c); });
    return result;
}

class FakeStreamIo {
  public:
    // A scripted read: data is returned as-is, an empty step without error is EOF,
    // and a step with an error fails with that errno.
    struct ReadStep {
        std::vector<std::byte> data;
        int error = 0;
    };

    // A scripted write: accepts at most max_bytes, or fails with error.
    struct WriteStep {
        std::size_t max_bytes = std::numeric_limits<std::size_t>::max();
        int error = 0;
    };

    // Once the read script is exhausted, reads fail with EAGAIN.
    void script_read(ReadStep step) { reads_.push_back(std::move(step)); }
    void script_read(std::string_view text) { reads_.push_back({.data = bytes(text)}); }
    void script_eof() { reads_.push_back({}); }
    void script_read_error(int error) { reads_.push_back({.data = {}, .error = error}); }

    // Once the write script is exhausted, writes accept everything.
    void script_write(WriteStep step) { writes_.push_back(step); }

    ssize_t read(int /*fd*/, std::span<std::byte> buffer) {
        ++read_calls_;
        if (reads_.empty()) {
            errno = EAGAIN;
            return -1;
        }
        ReadStep& step = reads_.front();
        if (step.error != 0) {
            errno = step.error;
            reads_.pop_front();
            return -1;
        }
        const auto count = std::min(buffer.size(), step.data.size());
        std::ranges::copy_n(step.data.begin(), static_cast<std::ptrdiff_t>(count), buffer.begin());
        step.data.erase(step.data.begin(), step.data.begin() + static_cast<std::ptrdiff_t>(count));
        if (step.data.empty() || count == 0) {
            reads_.pop_front();
        }
        return static_cast<ssize_t>(count);
    }

    ssize_t write(int /*fd*/, std::span<const std::byte> data) {
        ++write_calls_;
        WriteStep step;
        if (!writes_.empty()) {
            step = writes_.front();
            writes_.pop_front();
        }
        if (step.error != 0) {
            errno = step.error;
            return -1;
        }
        const auto count = std::min(data.size(), step.max_bytes);
        written_.insert(written_.end(), data.begin(),
                        data.begin() + static_cast<std::ptrdiff_t>(count));
        return static_cast<ssize_t>(count);
    }

    [[nodiscard]] const std::vector<std::byte>& written() const noexcept { return written_; }
    [[nodiscard]] int read_calls() const noexcept { return read_calls_; }
    [[nodiscard]] int write_calls() const noexcept { return write_calls_; }

  private:
    std::deque<ReadStep> reads_;
    std::deque<WriteStep> writes_;
    std::vector<std::byte> written_;
    int read_calls_ = 0;
    int write_calls_ = 0;
};

}  // namespace rfc862::test
