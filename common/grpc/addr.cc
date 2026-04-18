#include "addr.h"

#include <arpa/inet.h>
#include <netinet/in.h>

#include <cstdint>
#include <string>

bool parse_host_port_tail(const std::string& rest, std::string* host, uint32_t* port_out) {
    const size_t colon = rest.rfind(':');
    if (colon == std::string::npos || colon == 0) {
        return false;
    }
    *host = rest.substr(0, colon);
    const std::string port_str = rest.substr(colon + 1);
    unsigned long port_ul = 0;
    try {
        port_ul = std::stoul(port_str);
    } catch (...) {
        return false;
    }
    if (port_ul > 65535u) {
        return false;
    }
    *port_out = static_cast<uint32_t>(port_ul);
    return true;
}

bool parse_ipv6_bracketed(const std::string& rest, std::string* host, uint32_t* port_out) {
    if (rest.empty() || rest[0] != '[') {
        return false;
    }
    const size_t close_bracket = rest.find(']');
    if (close_bracket == std::string::npos || close_bracket < 2) {
        return false;
    }
    *host = rest.substr(1, close_bracket - 1);
    if (close_bracket + 1 >= rest.size() || rest[close_bracket + 1] != ':') {
        return false;
    }
    const std::string port_str = rest.substr(close_bracket + 2);
    unsigned long port_ul = 0;
    try {
        port_ul = std::stoul(port_str);
    } catch (...) {
        return false;
    }
    if (port_ul > 65535u) {
        return false;
    }
    *port_out = static_cast<uint32_t>(port_ul);
    return true;
}

bool grpc_peer_uri_to_net_ip_addr(const std::string& peer, net::IPAddr* out) {
    if (!out) {
        return false;
    }
    static const char kIpv4Scheme[] = "ipv4:";
    static const char kIpv6Scheme[] = "ipv6:";
    constexpr size_t kIpv4Len = sizeof(kIpv4Scheme) - 1;
    constexpr size_t kIpv6Len = sizeof(kIpv6Scheme) - 1;

    if (peer.size() > kIpv4Len && peer.compare(0, kIpv4Len, kIpv4Scheme) == 0) {
        const std::string rest = peer.substr(kIpv4Len);
        std::string host;
        uint32_t port = 0;
        if (!parse_host_port_tail(rest, &host, &port)) {
            return false;
        }
        in_addr bin{};
        if (inet_pton(AF_INET, host.c_str(), &bin) != 1) {
            return false;
        }
        out->clear_ipv6();
        net::IPV4Addr* v4 = out->mutable_ipv4();
        v4->mutable_addr()->assign(reinterpret_cast<const char*>(&bin.s_addr), 4);
        v4->set_port(port);
        return true;
    }

    if (peer.size() > kIpv6Len && peer.compare(0, kIpv6Len, kIpv6Scheme) == 0) {
        const std::string rest = peer.substr(kIpv6Len);
        std::string host;
        uint32_t port = 0;
        if (!parse_ipv6_bracketed(rest, &host, &port)) {
            return false;
        }
        in6_addr bin{};
        if (inet_pton(AF_INET6, host.c_str(), &bin) != 1) {
            return false;
        }
        out->clear_ipv4();
        net::IPV6Addr* v6 = out->mutable_ipv6();
        v6->mutable_addr()->assign(reinterpret_cast<const char*>(&bin), sizeof(bin));
        v6->set_port(port);
        return true;
    }

    return false;
}
