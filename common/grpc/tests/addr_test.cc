#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <cstring>

#include <grpc/addr.h>
#include <net.pb.h>

TEST(AddrTest, ParsesIpv4PeerUri) {
    net::IPAddr out;
    ASSERT_TRUE(grpc_peer_uri_to_net_ip_addr("ipv4:127.0.0.1:50051", &out));
    ASSERT_TRUE(out.has_ipv4());
    EXPECT_FALSE(out.has_ipv6());
    EXPECT_EQ(out.ipv4().port(), 50051u);
    ASSERT_EQ(out.ipv4().addr().size(), 4u);

    in_addr expected{};
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &expected), 1);
    EXPECT_EQ(std::memcmp(out.ipv4().addr().data(), &expected.s_addr, 4), 0);
}

TEST(AddrTest, ParsesIpv6PeerUri) {
    net::IPAddr out;
    ASSERT_TRUE(grpc_peer_uri_to_net_ip_addr("ipv6:[::1]:8080", &out));
    ASSERT_TRUE(out.has_ipv6());
    EXPECT_FALSE(out.has_ipv4());
    EXPECT_EQ(out.ipv6().port(), 8080u);
    ASSERT_EQ(out.ipv6().addr().size(), 16u);

    in6_addr expected{};
    ASSERT_EQ(inet_pton(AF_INET6, "::1", &expected), 1);
    EXPECT_EQ(std::memcmp(out.ipv6().addr().data(), &expected, 16), 0);
}

TEST(AddrTest, RejectsNullOut) {
    EXPECT_FALSE(grpc_peer_uri_to_net_ip_addr("ipv4:127.0.0.1:1", nullptr));
}

TEST(AddrTest, RejectsBadScheme) {
    net::IPAddr out;
    EXPECT_FALSE(grpc_peer_uri_to_net_ip_addr("unix:/tmp/foo", &out));
    EXPECT_FALSE(grpc_peer_uri_to_net_ip_addr("ipv4", &out));
}

TEST(AddrTest, RejectsBadHostOrPort) {
    net::IPAddr out;
    EXPECT_FALSE(grpc_peer_uri_to_net_ip_addr("ipv4:not-an-ip:1", &out));
    EXPECT_FALSE(grpc_peer_uri_to_net_ip_addr("ipv4:127.0.0.1:99999", &out));
    EXPECT_FALSE(grpc_peer_uri_to_net_ip_addr("ipv4:127.0.0.1", &out));
    EXPECT_FALSE(grpc_peer_uri_to_net_ip_addr("ipv6:::1:80", &out));
    EXPECT_FALSE(grpc_peer_uri_to_net_ip_addr("ipv6:[::1]", &out));
}
