// SPDX-License-Identifier: GPL-3.0-or-later
// RFC 862 Echo Protocol server: echoes TCP and UDP on port 7 until a termination
// signal arrives, then shuts down cleanly.

#include <cstdio>

import std;
import rfc862;

namespace {

int report(const char* what, const std::error_code& error) {
    std::println(stderr, "rfc862: {}: {}{}", what, error.message(),
                 error == std::errc::permission_denied ? " (requires CAP_NET_BIND_SERVICE)" : "");
    return 1;
}

int run(int argc) {
    if (argc > 1) {
        std::println(stderr, "usage: rfc862");
        return 2;
    }
    // Before any thread starts, so that every thread inherits the blocked signals.
    const auto guard = rfc862::SignalGuard::install();
    if (!guard) {
        return report("cannot install signal handling", guard.error());
    }
    auto server = rfc862::Server::start();
    if (!server) {
        return report("cannot serve port 7", server.error());
    }
    if (const auto signal = rfc862::run_until_signal(*server, *guard); !signal) {
        return report("cannot wait for signals", signal.error());
    }
    return 0;
}

}  // namespace

int main(int argc, char** /*argv*/) {
    try {
        return run(argc);
    } catch (...) {
        // Only printing diagnostics can throw; the stack has unwound and every
        // resource is released by now.
        return 1;
    }
}
