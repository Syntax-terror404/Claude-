#pragma once
// Minimal POSIX UDP transport for wire packets (unicast; swap the address for a
// multicast group to fan out). Blocking-free receive with a poll timeout.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <stdexcept>
#include <string>

#include "lob/wire.hpp"

namespace lob {

class UdpSocket {
public:
    UdpSocket() : fd_(::socket(AF_INET, SOCK_DGRAM, 0)) {
        if (fd_ < 0) throw std::runtime_error("socket() failed");
    }
    ~UdpSocket() { if (fd_ >= 0) ::close(fd_); }
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // Binds to ip:port (port 0 = pick one). Returns the bound port.
    std::uint16_t bind(const std::string& ip, std::uint16_t port, int rcvbuf = 4 << 20) {
        ::setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf);
        sockaddr_in a = addr(ip, port);
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0)
            throw std::runtime_error("bind() failed");
        socklen_t n = sizeof a;
        ::getsockname(fd_, reinterpret_cast<sockaddr*>(&a), &n);
        return ntohs(a.sin_port);
    }
    void connect(const std::string& ip, std::uint16_t port) {
        sockaddr_in a = addr(ip, port);
        if (::connect(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0)
            throw std::runtime_error("connect() failed");
    }
    bool send(const wire::Packet& p) {
        return ::send(fd_, p.data(), p.size(), 0) == ssize_t(p.size());
    }
    bool send_raw(const std::uint8_t* d, std::size_t n) {
        return ::send(fd_, d, n, 0) == ssize_t(n);
    }
    // Returns bytes received, 0 on timeout.
    std::size_t recv(std::uint8_t* buf, std::size_t cap, int timeout_ms) {
        pollfd pf{fd_, POLLIN, 0};
        if (::poll(&pf, 1, timeout_ms) <= 0) return 0;
        ssize_t n = ::recv(fd_, buf, cap, 0);
        return n > 0 ? std::size_t(n) : 0;
    }

private:
    static sockaddr_in addr(const std::string& ip, std::uint16_t port) {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        if (::inet_pton(AF_INET, ip.c_str(), &a.sin_addr) != 1)
            throw std::runtime_error("bad ip");
        return a;
    }
    int fd_;
};

}  // namespace lob
