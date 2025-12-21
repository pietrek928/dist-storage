#include "tcpv4.h"

#include "helpers.h"
#include "minion.pb.h"
#include "unique_fd.h"

#include <cstdint>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

void ipv4_convert_addr(const std::string &addr, in_addr *out) {
    if (!addr.size()) {
        out->s_addr = INADDR_ANY;
        return;
    }
    if (addr.size() != 4) {
        throw std::invalid_argument("Invalid IPv4 address string length " + std::to_string(addr.size());
    }
    memcpy(out, addr.data(), 4);
}

void ipv4_convert_addr(const in_addr &addr, std::string *out) {
    out->resize(4);
    memcpy(out->data(), &addr, 4);
}

void ipv4_convert_port(const uint32_t port, in_port_t *out) {
    *out = htons(port);
}

uint32_t ipv4_convert_port(const in_port_t port) {
    return ntohs(port);
}

socket_t tcpv4_new_socket(bool blocking) {
    int flags = blocking ? 0 : SOCK_NONBLOCK;
    socket_t fd;
    ccall(
        "creating socket", fd = socket(AF_INET, SOCK_STREAM | flags, 0)
    );
    return fd;
}

void tcpv4_close_socket(socket_t fd) {
    ccall("closing socket", close(fd));
}

void tcpv4_bind_port(socket_t fd, const minion::IPV4Addr &bind_addr) {
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    ipv4_convert_port(bind_addr.port(), &addr.sin_port);
    ipv4_convert_addr(bind_addr.addr(), &addr.sin_addr);

    ccall("binding port", bind(fd, (struct sockaddr*)(&addr), sizeof(addr)));
}

void tcpv4_connect(socket_t fd, const minion::IPV4Addr &dst_addr) {
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    ipv4_convert_port(dst_addr.port(), &addr.sin_port);
    ipv4_convert_addr(dst_addr.addr(), &addr.sin_addr);

    ccall("connecting", connect(fd, (struct sockaddr*)(&addr), sizeof(addr)));
}

int tcpv4_connect_unsafe(socket_t fd, const minion::IPV4Addr &dst_addr) {
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    ipv4_convert_port(dst_addr.port(), &addr.sin_port);
    ipv4_convert_addr(dst_addr.addr(), &addr.sin_addr);

    return connect(fd, (struct sockaddr*)(&addr), sizeof(addr));
}

void tcpv4_connect_abort(socket_t fd) {
    sockaddr_in addr;
    addr.sin_family = AF_UNSPEC;
    ccall("aborting connection", connect(fd, (struct sockaddr*)(&addr), sizeof(addr)));
}

void tcpv4_listen(socket_t fd, int backlog) {
    ccall("listening", listen(fd, backlog));
}

TCPv4AcceptResult tcpv4_accept(socket_t fd, bool create_blocking) {
    int flags = create_blocking ? 0 : SOCK_NONBLOCK;
    TCPv4AcceptResult ret;
    sockaddr_in addr;
    socklen_t addr_len;
    ret.new_fd = accept4(fd, (struct sockaddr*)(&addr), &addr_len, flags);
    ccall("accepting", ret.new_fd);
    ipv4_convert_addr(addr.sin_addr, ret.addr.mutable_addr());
    ret.addr.set_port(ipv4_convert_port(addr.sin_port));
    return ret;
}

TCPv4AcceptResult tcpv4_accept_unsafe(socket_t fd, bool create_blocking) {
    int flags = create_blocking ? 0 : SOCK_NONBLOCK;
    TCPv4AcceptResult ret;
    sockaddr_in addr;
    socklen_t addr_len;
    ret.new_fd = accept4(fd, (struct sockaddr*)(&addr), &addr_len, flags);
    ipv4_convert_addr(addr.sin_addr, ret.addr.mutable_addr());
    ret.addr.set_port(ipv4_convert_port(addr.sin_port));
    return ret;
}

socket_t tcpv4_hole_punch(const minion::HolePunchParameters &settings) {
    auto connect_tries = settings.connect_count();
    bool listen = settings.listen_first();
    float connect_sec = settings.connect_sec_start();
    do {
        auto start_timestamp = timespec_timestamp();

        unique_fd main_fd = tcpv4_new_socket(true);
        tcpv4_bind_port(main_fd, settings.src_addr);
        set_socket_timeout(main_fd, connect_sec);

        if (listen) {
            tcpv4_listen(main_fd);
            auto res = tcpv4_accept_unsafe(main_fd, true);
            if (res.new_fd >= 0) {
                if (res.addr.sin_addr.s_addr != settings.dst_addr) {
                    throw ConnectionError("Incorrent peer ip");
                }
                if (res.addr.sin_port != settings.dst_port) {
                    throw ConnectionError("Incorrent peer port");
                }
                return res.new_fd;
            }
        } else {
            if (!tcpv4_connect_unsafe(main_fd, settings.dst_port, settings.dst_addr)) {
                return main_fd.handle();
            }
        }

        auto end_timestamp = timespec_timestamp();
        sleep_sec(connect_sec - timespec_diff_sec(start_timestamp, end_timestamp));

        listen = !listen;
        connect_sec *= settings.connect_sec_scale;
        if (connect_sec > settings.connect_sec_max) {
            connect_sec = settings.connect_sec_max;
        }
    } while (--connect_tries);

    throw ConnectionError("TCPv4 hole punching failed");
}
