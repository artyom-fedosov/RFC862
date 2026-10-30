// SPDX-License-Identifier: GPL-3.0-or-later
// Test double for rfc862::DatagramIo: scripted incoming datagrams and send results
// (fake) that also records every datagram sent and where to (spy).

module;

#include <cerrno>

#include <netinet/in.h>
#include <sys/socket.h>

export module rfc862.test.fake_datagram_io;

import std;

export namespace rfc862::test {

struct Datagram {
    std::vector<std::byte> payload;
    sockaddr_in6 peer{};
};

[[nodiscard]] inline sockaddr_in6 ipv6_peer(std::uint16_t port) {
    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_loopback;
    address.sin6_port = htons(port);
    return address;
}

[[nodiscard]] inline bool same_peer(const sockaddr_in6& a, const sockaddr_in6& b) {
    return a.sin6_family == b.sin6_family && a.sin6_port == b.sin6_port &&
           std::memcmp(&a.sin6_addr, &b.sin6_addr, sizeof(a.sin6_addr)) == 0;
}

class FakeDatagramIo {
  public:
    // A scripted sendmmsg: accepts at most max_messages, or fails with error.
    struct SendStep {
        std::size_t max_messages = std::numeric_limits<std::size_t>::max();
        int error = 0;
    };

    // Once the incoming queue is empty, receiving fails with EAGAIN.
    void script_incoming(Datagram datagram) { incoming_.push_back(std::move(datagram)); }
    void script_receive_error(int error) { receive_errors_.push_back(error); }
    // Once the send script is exhausted, sends accept every message.
    void script_send(SendStep step) { sends_.push_back(step); }

    int recvmmsg(int /*fd*/, std::span<mmsghdr> messages) {
        ++receive_calls_;
        if (!receive_errors_.empty()) {
            errno = receive_errors_.front();
            receive_errors_.pop_front();
            return -1;
        }
        if (incoming_.empty()) {
            errno = EAGAIN;
            return -1;
        }
        std::size_t count = 0;
        for (; count < messages.size() && !incoming_.empty(); ++count) {
            const Datagram& datagram = incoming_.front();
            msghdr& header = messages[count].msg_hdr;
            const auto size = std::min(header.msg_iov[0].iov_len, datagram.payload.size());
            std::memcpy(header.msg_iov[0].iov_base, datagram.payload.data(), size);
            std::memcpy(header.msg_name, &datagram.peer, sizeof(datagram.peer));
            header.msg_namelen = sizeof(datagram.peer);
            messages[count].msg_len = static_cast<unsigned>(size);
            incoming_.pop_front();
        }
        return static_cast<int>(count);
    }

    int sendmmsg(int /*fd*/, std::span<mmsghdr> messages) {
        ++send_calls_;
        SendStep step;
        if (!sends_.empty()) {
            step = sends_.front();
            sends_.pop_front();
        }
        if (step.error != 0) {
            errno = step.error;
            return -1;
        }
        const auto count = std::min(messages.size(), step.max_messages);
        for (std::size_t i = 0; i < count; ++i) {
            const msghdr& header = messages[i].msg_hdr;
            Datagram datagram;
            const auto* data = static_cast<const std::byte*>(header.msg_iov[0].iov_base);
            datagram.payload.assign(data, data + header.msg_iov[0].iov_len);
            std::memcpy(&datagram.peer, header.msg_name, sizeof(datagram.peer));
            sent_.push_back(std::move(datagram));
        }
        return static_cast<int>(count);
    }

    [[nodiscard]] const std::vector<Datagram>& sent() const noexcept { return sent_; }
    [[nodiscard]] int receive_calls() const noexcept { return receive_calls_; }
    [[nodiscard]] int send_calls() const noexcept { return send_calls_; }

  private:
    std::deque<Datagram> incoming_;
    std::deque<int> receive_errors_;
    std::deque<SendStep> sends_;
    std::vector<Datagram> sent_;
    int receive_calls_ = 0;
    int send_calls_ = 0;
};

}  // namespace rfc862::test
