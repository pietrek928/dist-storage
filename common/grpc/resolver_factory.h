#pragma once

#include <grpcpp/grpcpp.h>

#include <core/resolver/resolver.h>
#include <core/resolver/resolver_factory.h>
#include <core/lib/iomgr/work_serializer.h>
#include "signaling.grpc.pb.h"


class NodeResolverFactory : public grpc_core::ResolverFactory {
public:
    explicit NodeResolverFactory(std::shared_ptr<Signaling::Stub> sig_stub)
        : sig_stub_(std::move(sig_stub)) {}

    absl::string_view scheme() const override { return "node"; }

    bool IsValidUri(const grpc_core::URI& uri) const override {
        return !uri.path().empty() || !uri.authority().empty();
    }

    grpc_core::OrphanablePtr<grpc_core::Resolver> CreateResolver(
        grpc_core::ResolverArgs args) const override {

        // Parse the hash from the URI (e.g., node:///my_node_hash)
        std::string hash = std::string(args.uri.path());
        if (hash.starts_with("/")) hash.erase(0, 1);
        if (hash.empty()) hash = std::string(args.uri.authority());

        return grpc_core::MakeOrphanable<NodeResolver>(
            std::move(hash), sig_stub_, std::move(args));
    }

private:
    std::shared_ptr<Signaling::Stub> sig_stub_;
};
