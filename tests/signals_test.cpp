// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <string>

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gtest/gtest.h>

import rfc862;
import rfc862.test.clients;
import rfc862.test.fake_stream_io;

namespace {

using rfc862::test::bytes;

constexpr int leaked_descriptors = 3;
constexpr int setup_failed = 4;

std::size_t open_descriptor_count() {
    const std::filesystem::directory_iterator entries{"/proc/self/fd"};
    return static_cast<std::size_t>(std::distance(begin(entries), end(entries)));
}

// Runs the server as the real program does, in a child process, so termination signals
// never reach the test runner. The child reports its port through a pipe and exits with
// 0 only if every descriptor it opened was released.
class ServerProcess : public ::testing::Test {
  protected:
    void SetUp() override {
        std::array<int, 2> report{-1, -1};
        std::array<int, 2> release{-1, -1};
        ASSERT_EQ(::pipe2(report.data(), O_CLOEXEC), 0);
        ASSERT_EQ(::pipe2(release.data(), O_CLOEXEC), 0);
        child = ::fork();
        ASSERT_GE(child, 0);
        if (child == 0) {
            ::close(report[0]);
            ::close(release[1]);
            run_child(report[1], release[0]);
        }
        ::close(report[1]);
        ::close(release[0]);
        report_.reset(report[0]);
        release_.reset(release[1]);
        ASSERT_EQ(::read(report_.get(), &port, sizeof(port)), static_cast<ssize_t>(sizeof(port)));
    }

    void TearDown() override {
        if (child > 0 && !reaped) {
            ::kill(child, SIGKILL);
            ::waitpid(child, nullptr, 0);
        }
    }

    // Serves until a termination signal, reports once everything is released, then
    // exits as soon as the parent lets it (closes the release pipe).
    [[noreturn]] static void run_child(int report, int release) {
        const auto before = open_descriptor_count();
        int status = setup_failed;
        {
            auto guard = rfc862::SignalGuard::install();
            auto server = rfc862::Server::start({.port = 0, .workers = 2});
            if (guard && server) {
                const std::uint16_t bound = server->tcp_port();
                if (::write(report, &bound, sizeof(bound)) == sizeof(bound)) {
                    status = rfc862::run_until_signal(*server, *guard) ? 0 : setup_failed;
                }
            }
        }
        if (status == 0 && open_descriptor_count() != before) {
            status = leaked_descriptors;
        }
        const char done = 'd';
        if (::write(report, &done, 1) == 1) {
            char ignored = 0;
            while (::read(release, &ignored, 1) > 0) {
            }
        }
        ::close(report);
        ::close(release);
        // Every server thread has been joined by now. std::exit runs LeakSanitizer in
        // ASan builds.
        std::exit(status);  // NOLINT(concurrency-mt-unsafe)
    }

    // Blocks until the child has shut the server down and released its resources.
    [[nodiscard]] bool wait_until_shut_down() const {
        char done = 0;
        return ::read(report_.get(), &done, 1) == 1 && done == 'd';
    }

    // Lets the child exit, waits for it and returns its exit status, or -signal if a
    // signal killed it.
    int reap() {
        release_.reset();
        int status = 0;
        ::waitpid(child, &status, 0);
        reaped = true;
        return WIFEXITED(status) ? WEXITSTATUS(status) : -WTERMSIG(status);
    }

    [[nodiscard]] rfc862::FileDescriptor connected_client() const {
        auto client = rfc862::test::connect_tcp(port);
        if (!rfc862::test::send_all(client, bytes("ping")) ||
            rfc862::test::receive_exactly(client, 4) != bytes("ping")) {
            return {};
        }
        return client;
    }

    // True if nothing listens on the port any more (TIME_WAIT is tolerated).
    [[nodiscard]] bool port_is_free() const {
        const rfc862::FileDescriptor probe{::socket(AF_INET6, SOCK_STREAM | SOCK_CLOEXEC, 0)};
        const int reuse = 1;
        ::setsockopt(probe.get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        sockaddr_in6 address{};
        address.sin6_family = AF_INET6;
        address.sin6_addr = in6addr_any;
        address.sin6_port = htons(port);
        return ::bind(probe.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) ==
                   0 &&
               ::listen(probe.get(), 1) == 0;
    }

    pid_t child = -1;
    bool reaped = false;
    rfc862::FileDescriptor report_;
    rfc862::FileDescriptor release_;
    std::uint16_t port = 0;
};

class TerminationSignal : public ServerProcess, public ::testing::WithParamInterface<int> {};

}  // namespace

TEST_P(TerminationSignal, ShutsDownCleanly) {
    // Given
    const auto client = connected_client();
    ASSERT_TRUE(client.valid());
    // When
    ASSERT_EQ(::kill(child, GetParam()), 0);
    const int status = reap();
    // Then
    EXPECT_EQ(status, 0);
    EXPECT_TRUE(rfc862::test::receives_eof(client));
    EXPECT_TRUE(port_is_free());
}

INSTANTIATE_TEST_SUITE_P(AllTerminationSignals, TerminationSignal,
                         ::testing::Values(SIGINT, SIGTERM, SIGHUP, SIGQUIT),
                         [](const auto& signal) {
                             return std::string{sigabbrev_np(signal.param)};
                         });

TEST_F(ServerProcess, SurvivesSigpipe) {
    // Given
    ASSERT_TRUE(connected_client().valid());
    // When
    ASSERT_EQ(::kill(child, SIGPIPE), 0);
    // Then
    EXPECT_TRUE(connected_client().valid());
    ASSERT_EQ(::kill(child, SIGTERM), 0);
    EXPECT_EQ(reap(), 0);
}

TEST_F(ServerProcess, ShutsDownCleanlyWhenSignalledTwice) {
    // Given
    const auto client = connected_client();
    ASSERT_TRUE(client.valid());
    // When
    ASSERT_EQ(::kill(child, SIGTERM), 0);
    ASSERT_EQ(::kill(child, SIGINT), 0);
    const int status = reap();
    // Then
    EXPECT_EQ(status, 0);
    EXPECT_TRUE(rfc862::test::receives_eof(client));
}

TEST_F(ServerProcess, ExitsNormallyWhenSignalledAfterShutdown) {
    // Given
    ASSERT_EQ(::kill(child, SIGTERM), 0);
    ASSERT_TRUE(wait_until_shut_down());
    // When
    ASSERT_EQ(::kill(child, SIGINT), 0);
    const int status = reap();
    // Then
    EXPECT_EQ(status, 0);
}
