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
        std::array<int, 2> pipe{-1, -1};
        ASSERT_EQ(::pipe2(pipe.data(), O_CLOEXEC), 0);
        child = ::fork();
        ASSERT_GE(child, 0);
        if (child == 0) {
            ::close(pipe[0]);
            run_child(pipe[1]);
        }
        ::close(pipe[1]);
        ASSERT_EQ(::read(pipe[0], &port, sizeof(port)), static_cast<ssize_t>(sizeof(port)));
        ::close(pipe[0]);
    }

    void TearDown() override {
        if (child > 0 && !reaped) {
            ::kill(child, SIGKILL);
            ::waitpid(child, nullptr, 0);
        }
    }

    [[noreturn]] static void run_child(int report) {
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
        ::close(report);
        // Every server thread has been joined by now. std::exit runs LeakSanitizer in
        // ASan builds.
        std::exit(status);  // NOLINT(concurrency-mt-unsafe)
    }

    // Waits for the child and returns its exit status, or -signal if a signal killed it.
    int reap() {
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
