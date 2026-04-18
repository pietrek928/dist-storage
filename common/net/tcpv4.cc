#include "tcpv4.h"

#include <cstdint>
#include <string>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/poll.h>
#include <unistd.h>

#include <utils/sys/time.h>
#include <utils/sys/socket.h>
#include "call_check.h"


void ipv4_convert_addr(const std::string &addr, in_addr *out) {
    if (!addr.size()) {
        out->s_addr = INADDR_ANY;
        return;
    }
    if (addr.size() != 4) {
        throw std::invalid_argument("Invalid IPv4 address string length " + std::to_string(addr.size()));
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

void tcpv4_set_blocking(socket_t fd, bool blocking) {
    int flags;
    ccall("getting socket flags", flags = fcntl(fd, F_GETFL, 0));
    if (blocking) {
        flags &= ~O_NONBLOCK;
    } else {
        flags |= O_NONBLOCK;
    }
    ccall("setting socket blocking", fcntl(fd, F_SETFL, flags));
}

void tcpv4_set_timeout(socket_t fd, float timeout_sec) {
    struct timeval tv;
    tv.tv_sec = static_cast<int>(timeout_sec);
    tv.tv_usec = static_cast<int>((timeout_sec - tv.tv_sec) * 1000000);
    ccall("setting socket recv timeout", setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv));
    ccall("setting socket send timeout", setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv));
}

void tcpv4_close_socket(socket_t fd) {
    ccall("closing socket", close(fd));
}

void tcpv4_port_reuse(int fd, int val) {
    ccall(
        "Allow multiple sockets to bind to the same local address/port pair",
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val))
    );
    ccall(
        "Specifically allow multiple sockets to SHARE the exact same port",
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val))
    );
}

void tcpv4_bind_port(socket_t fd, const net::IPV4Addr &bind_addr) {
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    ipv4_convert_port(bind_addr.port(), &addr.sin_port);
    ipv4_convert_addr(bind_addr.addr(), &addr.sin_addr);

    ccall("binding port", bind(fd, (struct sockaddr*)(&addr), sizeof(addr)));
}

void tcpv4_connect(socket_t fd, const net::IPV4Addr &dst_addr) {
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    ipv4_convert_port(dst_addr.port(), &addr.sin_port);
    ipv4_convert_addr(dst_addr.addr(), &addr.sin_addr);

    ccall("connecting", connect(fd, (struct sockaddr*)(&addr), sizeof(addr)));
}

ConnectStatus tcpv4_connect_unsafe(socket_t fd, const net::IPV4Addr &dst_addr) {
    sockaddr_in addr = {}; // Zero-initialize memory
    addr.sin_family = AF_INET;
    ipv4_convert_port(dst_addr.port(), &addr.sin_port);
    ipv4_convert_addr(dst_addr.addr(), &addr.sin_addr);

    int res = connect(fd, (struct sockaddr*)(&addr), sizeof(addr));
    if (res == 0) {
        return ConnectStatus::SUCCESS; // Connected instantly (rare on WAN, common on localhost)
    }

    // Evaluate the non-blocking error codes
    if (errno == EINPROGRESS || errno == EALREADY) {
        return ConnectStatus::IN_PROGRESS; // Handshake is in flight
    }

    if (errno == EISCONN) {
        return ConnectStatus::SUCCESS; // The handshake from a previous loop iteration finished!
    }

    // If we get ECONNREFUSED, ENETUNREACH, etc., it's a hard fail.
    return ConnectStatus::FAILED;
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

    sockaddr_in addr = {}; // Zero-initialize memory
    socklen_t addr_len = sizeof(addr); // Must be initialized!
    ret.new_fd = accept4(fd, (struct sockaddr*)(&addr), &addr_len, flags);

    if (ret.new_fd < 0) {
        return ret;
    }

    // Success! We have a connection. Parse the peer's address.
    ipv4_convert_addr(addr.sin_addr, ret.addr.mutable_addr());
    ret.addr.set_port(ipv4_convert_port(addr.sin_port));
    return ret;
}

void tcpv4_addr_from_socket(net::IPV4Addr* src_ipv4, socket_t sock) {
    sockaddr_in sin{};
    socklen_t len = sizeof(sin);
    ccall("getting socket name", getsockname(sock, reinterpret_cast<sockaddr*>(&sin), &len));
    src_ipv4->mutable_addr()->assign(reinterpret_cast<const char*>(&sin.sin_addr.s_addr), 4);
    src_ipv4->set_port(ntohs(sin.sin_port));
}

const net::IPV4Addr& get_hole_punch_ipv4_src(const net::HolePunchParameters& settings) {
    if (settings.src().addr_case() != net::IPAddr::kIpv4) {
        throw ConnectionError("hole punch parameters require IPv4 src");
    }
    return settings.src().ipv4();
}

const net::IPV4Addr& get_hole_punch_ipv4_dst(const net::HolePunchParameters& settings) {
    if (settings.dst().addr_case() != net::IPAddr::kIpv4) {
        throw ConnectionError("hole punch parameters require IPv4 dst");
    }
    return settings.dst().ipv4();
}

socket_t tcpv4_hole_punch(unique_fd listen_fd, const net::HolePunchParameters &settings) {
    const net::IPV4Addr& punch_src = get_hole_punch_ipv4_src(settings);
    const net::IPV4Addr& punch_dst = get_hole_punch_ipv4_dst(settings);

    // 1. Create the Listener Socket (Non-blocking: false)
    // unique_fd listen_fd = tcpv4_new_socket(false);
    // tcpv4_port_reuse(listen_fd);
    // tcpv4_bind_port(listen_fd, punch_src);
    tcpv4_listen(listen_fd);

    // 2. Create the Connector Socket (Non-blocking: false)
    unique_fd connect_fd = tcpv4_new_socket(false);
    tcpv4_port_reuse(connect_fd.handle());
    tcpv4_bind_port(connect_fd, punch_src);

    auto connect_tries = settings.connect_count();
    float connect_sec = settings.connect_sec_start();

    // We only call connect() once, then let the OS handle SYN retransmits,
    // UNLESS the connection is actively rejected, in which case we try again.
    bool fire_new_syn = true;

    do {
        auto start_timestamp = timespec_timestamp();

        // --- STEP A: Punch Outbound ---
        if (fire_new_syn) {
            ConnectStatus status = tcpv4_connect_unsafe(connect_fd, punch_dst);
            if (status == ConnectStatus::SUCCESS) {
                // We punched through!
                return connect_fd.handle();
            } else if (status == ConnectStatus::FAILED) {
                // Optional: Log the failure or recreate connect_fd if it was a hard reject
            }

            // If IN_PROGRESS, we just wait for poll().
            // If FAILED, it will instantly trigger POLLERR in the poll() below anyway.
            fire_new_syn = false;
        }

        struct pollfd fds[] = {
            {
                // Watch listener for incoming peer
                .fd = listen_fd,
                .events = POLLIN,
            },
            {
                // Watch connector for success (POLLOUT) or hard reject (POLLERR)
                .fd = connect_fd,
                .events = POLLOUT | POLLERR,
            }
        };

        // --- STEP B: Catch Inbound ---
        // Convert your backoff float to milliseconds
        int timeout_ms = static_cast<int>(connect_sec * 1000.0f);

        int res = poll(fds, 2, timeout_ms);
        if (res < 0) {
            if (errno == EINTR) continue; // Interrupted by signal, just loop
            throw ConnectionError("Poll failed during hole punch");
        }

        if (res > 0) {
            // Event 1: The peer's SYN reached our Listener!
            if (fds[0].revents & POLLIN) {
                auto accept_res = tcpv4_accept_unsafe(listen_fd, true); // Make the new socket blocking
                if (accept_res.new_fd >= 0) {
                    if (accept_res.addr.addr() == punch_dst.addr() &&
                        accept_res.addr.port() == punch_dst.port()) {
                        return accept_res.new_fd; // We caught them!
                    } else {
                        close(accept_res.new_fd); // Rogue scanner, drop it
                    }
                }
            }

            // Event 2: Our outbound connect() finished
            if (fds[1].revents & (POLLOUT | POLLERR | POLLHUP)) {
                // To find out if it succeeded or failed, we MUST check SO_ERROR
                int err = 0;
                socklen_t err_len = sizeof(err);
                getsockopt(connect_fd, SOL_SOCKET, SO_ERROR, &err, &err_len);

                if (err == 0) {
                    // Success! Simultaneous open or straight punch-through worked.
                    return connect_fd.handle();
                } else {
                    // The connection failed (e.g., ECONNREFUSED because we hit their NAT
                    // before their listener was ready).
                    // We must destroy the burned socket and queue a new one for the next loop.
                    fire_new_syn = true;
                    connect_fd = tcpv4_new_socket(false); // Assuming unique_fd supports move assignment
                    tcpv4_port_reuse(connect_fd);
                    tcpv4_bind_port(connect_fd, punch_src);
                }
            }
        }

        // --- STEP C: Backoff Calculation ---
        // We reach here if poll() timed out, or if we got a rogue connection/failed connect.
        connect_sec *= settings.connect_sec_scale();
        if (connect_sec > settings.connect_sec_max()) {
            connect_sec = settings.connect_sec_max();
        }
    } while (--connect_tries);

    throw ConnectionError("TCPv4 hole punching failed");
}

ReservedSocket tcpv4_bind_random_port(
    std::random_device &rd,
    uint16_t min_port, uint16_t max_port, uint32_t local_ip_addr,
    int tries
) {
    ReservedSocket ret;
    if (min_port > max_port) return ret;

    // 1. Setup high-quality random number generator
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint16_t> dist(min_port, max_port);

    // Try up to 50 random ports before giving up
    for (int i = 0; i < tries; ++i) {
        uint16_t target_port = dist(gen);

        // 2. Create the socket (Non-blocking)
        unique_fd fd = tcpv4_new_socket(false);

        // 3. CRITICAL: Inject REUSE options immediately
        tcpv4_port_reuse(fd);

        // 4. Prepare POSIX bind struct
        sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(target_port),
            .sin_addr = {
                .s_addr = local_ip_addr, // Usually INADDR_ANY (0)
            }
        };

        // 5. Try to bind
        if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            // Success! The OS granted us this port.
            // Because we hold the fd, no other app can take it.
            return ReservedSocket{fd.handle(), target_port};
        }

        // If bind fails (EADDRINUSE), the loop naturally continues and tries another port,
        // and unique_fd automatically closes the failed socket as it goes out of scope.
    }

    return ret; // Failed to find a free port in 50 tries
}
