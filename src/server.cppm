// SPDX-License-Identifier: GPL-3.0-or-later
// The echo server: one independent epoll loop per thread, all sharing the service
// port through SO_REUSEPORT so the kernel balances clients without any locking.

module;

#include <sys/epoll.h>

export module rfc862:server;

import std;
import :common;
import :epoll;
import :fd;
import :socket;
import :tcp_echo;
import :udp_echo;

namespace rfc862 {

// Size of each worker's shared TCP read buffer.
constexpr std::size_t scratch_size = 64UZ * 1024;
// Connections accepted per readiness event before serving other sockets.
constexpr std::size_t max_accepts_per_event = 64;

// Epoll tags. Connections are tagged with their (non-negative) descriptor, so these
// values can never collide with one.
constexpr std::uint64_t listener_tag = ~std::uint64_t{0};
constexpr std::uint64_t udp_tag = ~std::uint64_t{0} - 1;
constexpr std::uint64_t stop_tag = ~std::uint64_t{0} - 2;

class Worker {
  public:
    [[nodiscard]] static Result<std::unique_ptr<Worker>> create(std::uint16_t tcp_port,
                                                                std::uint16_t udp_port,
                                                                const EventFd& stop) {
        auto epoll = Epoll::create();
        if (!epoll) {
            return std::unexpected{epoll.error()};
        }
        auto listener = make_tcp_listener(tcp_port);
        if (!listener) {
            return std::unexpected{listener.error()};
        }
        auto udp = make_udp_socket(udp_port);
        if (!udp) {
            return std::unexpected{udp.error()};
        }
        // Everything is level-triggered: a socket left with work after its fairness
        // cap is simply reported again on the next round.
        for (const auto& [fd, tag] :
             {std::pair{listener->get(), listener_tag}, std::pair{udp->get(), udp_tag},
              std::pair{stop.fd(), stop_tag}}) {
            if (auto added = epoll->add(fd, EPOLLIN, tag); !added) {
                return std::unexpected{added.error()};
            }
        }
        return std::unique_ptr<Worker>{
            new Worker{std::move(*epoll), std::move(*listener), std::move(*udp)}};
    }

    [[nodiscard]] int listener_fd() const noexcept { return listener_.get(); }
    [[nodiscard]] int udp_fd() const noexcept { return udp_.get(); }

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
                    case udp_tag:
                        udp_echo_.echo_all(udp_.get());
                        break;
                    default:
                        serve(static_cast<int>(event.data.u64), event.events);
                        break;
                }
            }
        }
    }

  private:
    Worker(Epoll epoll, FileDescriptor listener, FileDescriptor udp)
        : epoll_{std::move(epoll)},
          listener_{std::move(listener)},
          udp_{std::move(udp)},
          scratch_(scratch_size) {}

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
    FileDescriptor udp_;
    UdpEchoBatch udp_echo_;
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
        std::uint16_t tcp_port = options.port;
        std::uint16_t udp_port = options.port;
        for (unsigned i = 0; i < count; ++i) {
            auto worker = Worker::create(tcp_port, udp_port, *server.stop_);
            if (!worker) {
                return std::unexpected{worker.error()};
            }
            if (i == 0) {
                // With port 0 the kernel picks the ports; the other workers share them.
                auto bound_tcp = local_port((*worker)->listener_fd());
                if (!bound_tcp) {
                    return std::unexpected{bound_tcp.error()};
                }
                auto bound_udp = local_port((*worker)->udp_fd());
                if (!bound_udp) {
                    return std::unexpected{bound_udp.error()};
                }
                tcp_port = *bound_tcp;
                udp_port = *bound_udp;
            }
            server.workers_.push_back(std::move(*worker));
        }
        server.tcp_port_ = tcp_port;
        server.udp_port_ = udp_port;
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
    [[nodiscard]] std::uint16_t udp_port() const noexcept { return udp_port_; }

    // Asks every worker to finish. Safe to call from any thread, any number of times.
    void stop() const noexcept {
        if (stop_) {
            stop_->notify();
        }
    }

    // Blocks until every worker has finished, then releases their resources: client
    // connections are closed in an orderly way and the service port is freed.
    void wait() {
        for (auto& thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        threads_.clear();
        workers_.clear();
    }

  private:
    Server() = default;

    // Declaration order matters: threads are joined before the workers they run and
    // the stop eventfd they watch are destroyed.
    std::unique_ptr<EventFd> stop_;
    std::vector<std::unique_ptr<Worker>> workers_;
    std::vector<std::jthread> threads_;
    std::uint16_t tcp_port_ = 0;
    std::uint16_t udp_port_ = 0;
};

}  // namespace rfc862
