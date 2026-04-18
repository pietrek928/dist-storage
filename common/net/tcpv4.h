#pragma once

#include <random>
#include <string>
#include <netinet/in.h>

#include <net.pb.h>
#include <utils/unique_fd.h>


typedef int socket_t;

enum class ConnectStatus {
    SUCCESS,      // Connected instantly, or previously connected
    IN_PROGRESS,  // SYN sent, handshake happening in the background
    FAILED        // Hard failure (Connection refused, network unreachable, etc.)
};

typedef struct TCPv4AcceptResult {
    socket_t new_fd = -1;
    net::IPV4Addr addr = {};
} TCPv4AcceptResult;


struct ReservedSocket {
    unique_fd fd;
    uint16_t port = 0;
};

socket_t tcpv4_new_socket(bool blocking = true);
void tcpv4_set_blocking(socket_t fd, bool blocking = true);
void tcpv4_set_timeout(socket_t fd, float timeout_sec);
void tcpv4_close_socket(socket_t fd);
void tcpv4_port_reuse(int fd, int val = 1);
void tcpv4_bind_port(socket_t fd, const net::IPV4Addr &bind_addr);
void tcpv4_connect(socket_t fd, const net::IPV4Addr &dst_addr);
ConnectStatus tcpv4_connect_unsafe(socket_t fd, const net::IPV4Addr &dst_addr);
void tcpv4_connect_abort(socket_t fd);
void tcpv4_listen(socket_t fd, int backlog = 1);
TCPv4AcceptResult tcpv4_accept(socket_t fd, bool create_blocking = true);
TCPv4AcceptResult tcpv4_accept_unsafe(socket_t fd, bool create_blocking);
void tcpv4_addr_from_socket(net::IPV4Addr* src_ipv4, socket_t sock);

socket_t tcpv4_hole_punch(unique_fd listen_fd, const net::HolePunchParameters &settings);
ReservedSocket tcpv4_bind_random_port(
    std::random_device &rd,
    uint16_t min_port, uint16_t max_port, uint32_t local_ip_addr,
    int tries = 32
);
