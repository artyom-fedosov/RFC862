// SPDX-License-Identifier: GPL-3.0-or-later
// The echo server: one independent epoll loop per thread, all sharing the service
// port through SO_REUSEPORT so the kernel balances clients without any locking.

module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sys/epoll.h>

export module rfc862:server;

import :common;
import :epoll;
import :fd;
import :socket;
import :tcp_echo;

namespace rfc862 {

// Size of each worker's shared TCP read buffer.
constexpr std::size_t scratch_size = 64UZ * 1024;
// Connections accepted per readiness event before serving other sockets.
constexpr std::size_t max_accepts_per_event = 64;

// Epoll tags. Connections are tagged with their (non-negative) descriptor, so these
// values can never collide with one.
constexpr std::uint64_t listener_tag = ~std::uint64_t{0};
constexpr std::uint64_t stop_tag = ~std::uint64_t{0} - 1;

class Worker {
  public:
    [[nodiscard]] static Result<std::unique_ptr<Worker>> create(std::uint16_t port,
                                                                const EventFd& stop) {
        auto epoll = Epoll::create();
        if (!epoll) {
            return std::unexpected{epoll.error()};
        }
        auto listener = make_tcp_listener(port);
        if (!listener) {
            return std::unexpected{listener.error()};
        }
        // Everything is level-triggered: a socket left with work after its fairness
        // cap is simply reported again on the next round.
        for (const auto& [fd, tag] :
             {std::pair{listener->get(), listener_tag}, std::pair{stop.fd(), stop_tag}}) {
            if (auto added = epoll->add(fd, EPOLLIN, tag); !added) {
                return std::unexpected{added.error()};
            }
        }
        return std::unique_ptr<Worker>{new Worker{std::move(*epoll), std::move(*listener)}};
    }

    [[nodiscard]] int listener_fd() const noexcept { return listener_.get(); }

    // Serves clients until the stop eventfd is signalled.
    void run() {
        std::array<epoll_event, 256> buffer{};
        for (;;) {
            const auto events = epoll_.wait(buffer, -1);
            if (!events) {
                return;
            }
            for (const epoll_event& event : *events) {
                switch (event.data.u64) {
                    case stop_tag:
                        return;
                    case listener_tag:
                        accept_clients();
                        break;
                    default:
                        serve(static_cast<int>(event.data.u64), event.events);
                        break;
                }
            }
        }
    }

  private:
    Worker(Epoll epoll, FileDescriptor listener)
        : epoll_{std::move(epoll)}, listener_{std::move(listener)}, scratch_(scratch_size) {}

    void accept_clients() {
        for (std::size_t accepted = 0; accepted < max_accepts_per_event; ++accepted) {
            auto client = accept_connection(listener_.get());
            if (!client) {
                if (client.error() == std::errc::connection_aborted) {
                    continue;
                }
                // EAGAIN: no more pending clients. Anything else (e.g. EMFILE) is
                // retried on the next round, since the listener stays readable.
                return;
            }
            const int fd = client->get();
            if (epoll_.add(fd, EPOLLIN | EPOLLRDHUP, static_cast<std::uint64_t>(fd))) {
                connections_.emplace(fd, TcpEchoConnection{std::move(*client)});
            }
        }
    }

    void serve(int fd, std::uint32_t events) {
        const auto found = connections_.find(fd);
        if (found == connections_.end()) {
            return;
        }
        TcpEchoConnection& connection = found->second;
        Progress progress = Progress::Closed;
        if ((events & EPOLLERR) != 0) {
            progress = Progress::Closed;
        } else if ((events & EPOLLOUT) != 0) {
            progress = connection.on_writable();
            if (progress == Progress::WantRead && !watch(fd, EPOLLIN | EPOLLRDHUP)) {
                progress = Progress::Closed;
            }
        } else {
            progress = connection.on_readable(scratch_);
            if (progress == Progress::WantWrite && !watch(fd, EPOLLOUT)) {
                progress = Progress::Closed;
            }
        }
        if (progress == Progress::Closed) {
            // Closing the descriptor also removes it from the epoll set.
            connections_.erase(found);
        }
    }

    bool watch(int fd, std::uint32_t events) {
        return epoll_.modify(fd, events, static_cast<std::uint64_t>(fd)).has_value();
    }

    Epoll epoll_;
    FileDescriptor listener_;
    std::vector<std::byte> scratch_;
    std::unordered_map<int, TcpEchoConnection> connections_;
};

}  // namespace rfc862

export namespace rfc862 {

struct ServerOptions {
    std::uint16_t port = echo_port;
    // 0 means one worker per hardware thread.
    unsigned workers = 0;
};

class Server {
  public:
    // Binds every worker's sockets before starting any thread, so configuration errors
    // such as EACCES on port 7 are reported here.
    [[nodiscard]] static Result<Server> start(ServerOptions options = {}) {
        Server server;
        auto stop = EventFd::create();
        if (!stop) {
            return std::unexpected{stop.error()};
        }
        server.stop_ = std::make_unique<EventFd>(std::move(*stop));
        const unsigned count = options.workers != 0
                                   ? options.workers
                                   : std::max(1U, std::thread::hardware_concurrency());
        std::uint16_t port = options.port;
        for (unsigned i = 0; i < count; ++i) {
            auto worker = Worker::create(port, *server.stop_);
            if (!worker) {
                return std::unexpected{worker.error()};
            }
            if (i == 0) {
                // With port 0 the kernel picks a port; the other workers share it.
                auto bound = local_port((*worker)->listener_fd());
                if (!bound) {
                    return std::unexpected{bound.error()};
                }
                port = *bound;
            }
            server.workers_.push_back(std::move(*worker));
        }
        server.tcp_port_ = port;
        for (const auto& worker : server.workers_) {
            server.threads_.emplace_back([&worker = *worker] { worker.run(); });
        }
        return server;
    }

    Server(Server&&) noexcept = default;
    Server& operator=(Server&&) = delete;
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    ~Server() {
        stop();
        wait();
    }

    [[nodiscard]] std::uint16_t tcp_port() const noexcept { return tcp_port_; }

    // Asks every worker to finish. Safe to call from any thread, any number of times.
    void stop() const noexcept {
        if (stop_) {
            stop_->notify();
        }
    }

    // Blocks until every worker has finished.
    void wait() {
        for (auto& thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

  private:
    Server() = default;

    // Declaration order matters: threads are joined before the workers they run and
    // the stop eventfd they watch are destroyed.
    std::unique_ptr<EventFd> stop_;
    std::vector<std::unique_ptr<Worker>> workers_;
    std::vector<std::jthread> threads_;
    std::uint16_t tcp_port_ = 0;
};

}  // namespace rfc862
