// SPDX-License-Identifier: GPL-3.0-or-later

#include <cerrno>

#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

import std;
import rfc862;
import rfc862.test.fake_stream_io;

namespace {

using rfc862::test::bytes;
using rfc862::test::FakeStreamIo;
using Connection = rfc862::BasicTcpEchoConnection<FakeStreamIo>;

class FakeConnection : public ::testing::Test {
  protected:
    Connection connection{rfc862::FileDescriptor{}};
    std::array<std::byte, 64> scratch{};

    FakeStreamIo& io() { return connection.io(); }
};

}  // namespace

TEST_F(FakeConnection, EchoesReadBytesAndWaitsForMore) {
    // Given
    io().script_read("abc");
    // When
    const auto progress = connection.on_readable(scratch);
    // Then
    EXPECT_EQ(progress, rfc862::Progress::WantRead);
    EXPECT_EQ(io().written(), bytes("abc"));
    EXPECT_EQ(connection.pending_bytes(), 0U);
}

TEST_F(FakeConnection, KeepsUnwrittenBytesPendingOnShortWrite) {
    // Given
    io().script_read("hello");
    io().script_write({.max_bytes = 2});
    io().script_write({.error = EAGAIN});
    // When
    const auto progress = connection.on_readable(scratch);
    // Then
    EXPECT_EQ(progress, rfc862::Progress::WantWrite);
    EXPECT_EQ(io().written(), bytes("he"));
    EXPECT_EQ(connection.pending_bytes(), 3U);
}

TEST_F(FakeConnection, FlushesPendingBytesInOrderWhenWritable) {
    // Given
    io().script_read("hello");
    io().script_write({.max_bytes = 2});
    io().script_write({.error = EAGAIN});
    ASSERT_EQ(connection.on_readable(scratch), rfc862::Progress::WantWrite);
    // When
    const auto progress = connection.on_writable();
    // Then
    EXPECT_EQ(progress, rfc862::Progress::WantRead);
    EXPECT_EQ(io().written(), bytes("hello"));
    EXPECT_EQ(connection.pending_bytes(), 0U);
}

TEST_F(FakeConnection, KeepsWaitingWhilePendingBytesCannotBeWritten) {
    // Given
    io().script_read("hello");
    io().script_write({.max_bytes = 1});
    io().script_write({.error = EAGAIN});
    ASSERT_EQ(connection.on_readable(scratch), rfc862::Progress::WantWrite);
    io().script_write({.max_bytes = 2});
    io().script_write({.error = EAGAIN});
    // When
    const auto progress = connection.on_writable();
    // Then
    EXPECT_EQ(progress, rfc862::Progress::WantWrite);
    EXPECT_EQ(io().written(), bytes("hel"));
    EXPECT_EQ(connection.pending_bytes(), 2U);
}

TEST_F(FakeConnection, ClosesWhenPeerFinishesSending) {
    // Given
    io().script_read("bye");
    io().script_eof();
    // When
    const auto progress = connection.on_readable(scratch);
    // Then
    EXPECT_EQ(progress, rfc862::Progress::Closed);
    EXPECT_EQ(io().written(), bytes("bye"));
}

TEST_F(FakeConnection, ClosesOnReadError) {
    // Given
    io().script_read_error(ECONNRESET);
    // When
    const auto progress = connection.on_readable(scratch);
    // Then
    EXPECT_EQ(progress, rfc862::Progress::Closed);
}

TEST_F(FakeConnection, RetriesInterruptedRead) {
    // Given
    io().script_read_error(EINTR);
    io().script_read("ok");
    // When
    const auto progress = connection.on_readable(scratch);
    // Then
    EXPECT_EQ(progress, rfc862::Progress::WantRead);
    EXPECT_EQ(io().written(), bytes("ok"));
}

TEST_F(FakeConnection, ClosesOnWriteError) {
    // Given
    io().script_read("data");
    io().script_write({.error = EPIPE});
    // When
    const auto progress = connection.on_readable(scratch);
    // Then
    EXPECT_EQ(progress, rfc862::Progress::Closed);
}

TEST_F(FakeConnection, ClosesOnWriteErrorWhileFlushing) {
    // Given
    io().script_read("data");
    io().script_write({.error = EAGAIN});
    ASSERT_EQ(connection.on_readable(scratch), rfc862::Progress::WantWrite);
    io().script_write({.error = ECONNRESET});
    // When
    const auto progress = connection.on_writable();
    // Then
    EXPECT_EQ(progress, rfc862::Progress::Closed);
}

TEST_F(FakeConnection, RetriesInterruptedWrite) {
    // Given
    io().script_read("data");
    io().script_write({.error = EINTR});
    // When
    const auto progress = connection.on_readable(scratch);
    // Then
    EXPECT_EQ(progress, rfc862::Progress::WantRead);
    EXPECT_EQ(io().written(), bytes("data"));
}

TEST_F(FakeConnection, YieldsAfterReadLimitSoOtherClientsAreServed) {
    // Given
    for (std::size_t i = 0; i < rfc862::max_reads_per_event * 2; ++i) {
        io().script_read("x");
    }
    // When
    const auto progress = connection.on_readable(scratch);
    // Then
    EXPECT_EQ(progress, rfc862::Progress::WantRead);
    EXPECT_EQ(static_cast<std::size_t>(io().read_calls()), rfc862::max_reads_per_event);
}

namespace {

class ConnectedSocketPair : public ::testing::Test {
  protected:
    void SetUp() override {
        std::array<int, 2> fds{-1, -1};
        ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, fds.data()),
                  0);
        connection = rfc862::TcpEchoConnection{rfc862::FileDescriptor{fds[0]}};
        peer.reset(fds[1]);
    }

    void send(const std::vector<std::byte>& data) const {
        ASSERT_EQ(::write(peer.get(), data.data(), data.size()), static_cast<ssize_t>(data.size()));
    }

    void shrink_send_buffer() const {
        const int size = 4096;
        ASSERT_EQ(::setsockopt(connection.fd(), SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)), 0);
    }

    [[nodiscard]] static std::vector<std::byte> pattern(std::size_t size) {
        std::vector<std::byte> result(size);
        for (std::size_t i = 0; i < size; ++i) {
            result[i] = static_cast<std::byte>(i * 31 % 251);
        }
        return result;
    }

    [[nodiscard]] std::vector<std::byte> receive_available() const {
        std::vector<std::byte> result;
        std::array<std::byte, 4096> chunk{};
        ssize_t count = 0;
        while ((count = ::read(peer.get(), chunk.data(), chunk.size())) > 0) {
            result.insert(result.end(), chunk.begin(), chunk.begin() + count);
        }
        return result;
    }

    rfc862::TcpEchoConnection connection{rfc862::FileDescriptor{}};
    rfc862::FileDescriptor peer;
    std::vector<std::byte> scratch = std::vector<std::byte>(64UZ * 1024);
};

}  // namespace

TEST_F(ConnectedSocketPair, EchoesPayloadUnchanged) {
    // Given
    send(bytes("hello, echo"));
    // When
    const auto progress = connection.on_readable(scratch);
    // Then
    EXPECT_EQ(progress, rfc862::Progress::WantRead);
    EXPECT_EQ(receive_available(), bytes("hello, echo"));
}

TEST_F(ConnectedSocketPair, PreservesArbitraryBinaryBytes) {
    // Given
    std::vector<std::byte> payload(256);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<std::byte>(i);
    }
    send(payload);
    // When
    const auto progress = connection.on_readable(scratch);
    // Then
    EXPECT_EQ(progress, rfc862::Progress::WantRead);
    EXPECT_EQ(receive_available(), payload);
}

TEST_F(ConnectedSocketPair, AppliesBackpressureWhenSocketBufferIsFull) {
    // Given
    shrink_send_buffer();
    send(pattern(64UZ * 1024));
    // When
    const auto progress = connection.on_readable(scratch);
    // Then
    EXPECT_EQ(progress, rfc862::Progress::WantWrite);
    EXPECT_GT(connection.pending_bytes(), 0U);
}

TEST_F(ConnectedSocketPair, DeliversEverythingInOrderAfterBackpressure) {
    // Given
    shrink_send_buffer();
    const auto payload = pattern(64UZ * 1024);
    send(payload);
    auto progress = connection.on_readable(scratch);
    ASSERT_EQ(progress, rfc862::Progress::WantWrite);
    std::vector<std::byte> echoed;
    // When
    while (echoed.size() < payload.size() && progress != rfc862::Progress::Closed) {
        const auto received = receive_available();
        echoed.insert(echoed.end(), received.begin(), received.end());
        progress = progress == rfc862::Progress::WantWrite ? connection.on_writable()
                                                           : connection.on_readable(scratch);
    }
    // Then
    EXPECT_EQ(echoed, payload);
    EXPECT_EQ(connection.pending_bytes(), 0U);
}

TEST_F(ConnectedSocketPair, ClosesAfterPeerShutsDownWriting) {
    // Given
    send(bytes("last words"));
    ASSERT_EQ(::shutdown(peer.get(), SHUT_WR), 0);
    // When
    const auto progress = connection.on_readable(scratch);
    // Then
    EXPECT_EQ(progress, rfc862::Progress::Closed);
    EXPECT_EQ(receive_available(), bytes("last words"));
}
