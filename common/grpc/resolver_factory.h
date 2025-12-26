

class NodeResolverFactory : public grpc_core::ResolverFactory {
public:
    NodeResolverFactory(std::shared_ptr<Signaling::Stub> sig_stub)
        : sig_stub_(std::move(sig_stub)) {}

    // The scheme we handle: "node://..."
    absl::string_view scheme() const override { return "node"; }

    bool IsValidUri(const grpc_core::URI& uri) const override {
        return !uri.path().empty() || !uri.authority().empty();
    }

    grpc_core::OrphanablePtr<grpc_core::Resolver> CreateResolver(
        grpc_core::ResolverArgs args) const override {

        // Extract hash from node:///my_hash (path) or node://my_hash (authority)
        std::string hash = std::string(args.uri.path());
        if (hash.empty()) hash = std::string(args.uri.authority());
        if (hash.starts_with("/")) hash.erase(0, 1);

        return grpc_core::MakeOrphanable<NodeResolver>(
            std::move(hash), sig_stub_, std::move(args));
    }

private:
    std::shared_ptr<Signaling::Stub> sig_stub_;
};
