// SPDX-License-Identifier: GPL-3.0-or-later
// Orderly shutdown on termination signals.
//
// The signals are blocked in every thread and read synchronously from a signalfd on
// the main thread, so no code ever runs in asynchronous signal context. A termination
// signal therefore leads to a normal shutdown in which RAII releases every resource.
// SIGKILL and SIGSTOP cannot be caught by any program; after them (and after crashes
// such as SIGSEGV) the kernel itself closes every descriptor and frees all memory.

module;

#include <array>
#include <cerrno>
#include <csignal>
#include <expected>
#include <system_error>
#include <utility>

#include <pthread.h>
#include <sys/signalfd.h>
#include <unistd.h>

export module rfc862:signals;

import :common;
import :fd;
import :server;

export namespace rfc862 {

inline constexpr std::array termination_signals{SIGINT, SIGTERM, SIGHUP, SIGQUIT};

// Installing is a one-way, process-wide change: the termination signals stay blocked
// for the rest of the process lifetime. Unblocking them again would let a signal that
// arrives during the last moments of shutdown (e.g. a second Ctrl+C) kill the process
// with its default action instead of letting it exit normally; a signal that arrives
// then simply stays pending and is discarded at exit.
class SignalGuard {
  public:
    // Must be called before any thread is started: threads inherit the blocked mask,
    // so the termination signals are never delivered asynchronously to any of them.
    [[nodiscard]] static Result<SignalGuard> install() {
        const sigset_t mask = termination_mask();
        if (const int error = ::pthread_sigmask(SIG_BLOCK, &mask, nullptr); error != 0) {
            return std::unexpected{std::error_code{error, std::system_category()}};
        }
        SignalGuard guard{FileDescriptor{::signalfd(-1, &mask, SFD_CLOEXEC)}};
        if (!guard.fd_) {
            return fail();
        }
        // A write to a client that already disconnected must fail with EPIPE and close
        // just that connection instead of killing the process.
        struct sigaction ignore{};
        ignore.sa_handler = SIG_IGN;
        if (::sigaction(SIGPIPE, &ignore, nullptr) != 0) {
            return fail();
        }
        return guard;
    }

    // Blocks until a termination signal arrives and returns its number.
    [[nodiscard]] Result<int> wait() const {
        signalfd_siginfo info{};
        for (;;) {
            const ssize_t size = ::read(fd_.get(), &info, sizeof(info));
            if (std::cmp_equal(size, sizeof(info))) {
                return static_cast<int>(info.ssi_signo);
            }
            if (size < 0 && errno != EINTR) {
                return fail();
            }
        }
    }

  private:
    explicit SignalGuard(FileDescriptor fd) noexcept : fd_{std::move(fd)} {}

    static sigset_t termination_mask() noexcept {
        sigset_t mask;
        sigemptyset(&mask);
        for (const int signal : termination_signals) {
            sigaddset(&mask, signal);
        }
        return mask;
    }

    FileDescriptor fd_;
};

// Serves until a termination signal arrives, then stops every worker and waits until
// all of them have released their connections and sockets. Returns the signal number.
Result<int> run_until_signal(Server& server, const SignalGuard& guard) {
    const auto signal = guard.wait();
    server.stop();
    server.wait();
    return signal;
}

}  // namespace rfc862
