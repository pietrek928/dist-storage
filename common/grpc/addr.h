#pragma once

#include <string>

#include <net.pb.h>

/// Parses gRPC `ServerContext::peer()` URI (`ipv4:host:port` or `ipv6:[host]:port`) into
/// the appropriate branch of net::IPAddr.
bool grpc_peer_uri_to_net_ip_addr(const std::string& peer, net::IPAddr* out);
