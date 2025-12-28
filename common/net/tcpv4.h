#pragma once

#include <netinet/in.h>

#include <net.pb.h>


typedef int socket_t;

typedef struct TCPv4AcceptResult {
    socket_t new_fd;
    net::IPV4Addr addr;
} TCPv4AcceptResult;


socket_t tcpv4_new_socket(bool blocking = true);
void tcpv4_close_socket(socket_t fd);
void tcpv4_bind_port(socket_t fd, const net::IPV4Addr &bind_addr);
void tcpv4_connect(socket_t fd, const net::IPV4Addr &dst_addr);
int tcpv4_connect_unsafe(socket_t fd, const net::IPV4Addr &dst_addr);
void tcpv4_connect_abort(socket_t fd);
void tcpv4_listen(socket_t fd, int backlog = 1);
TCPv4AcceptResult tcpv4_accept(socket_t fd, bool create_blocking = true);
TCPv4AcceptResult tcpv4_accept_unsafe(socket_t fd, bool create_blocking);
socket_t tcpv4_hole_punch(const net::HolePunchParameters &settings);
