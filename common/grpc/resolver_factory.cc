#include "resolver_factory.h"

#include <grpcpp/grpcpp.h>
#include <core/config/core_configuration.h>
#include <core/resolver/resolver_factory.h>

#include <utils/guard_ptr.h>
#include <message.grpc.pb.h>

#include "ref_counted_arg.h"
#include "resolver.h"


class NodeResolverFactory : public grpc_core::ResolverFactory {
public:
    static constexpr char kMessageStubArgKey[] = "grpc.custom.message_stub";

    absl::string_view scheme() const override;
    bool IsValidUri(const grpc_core::URI& uri) const override;
    std::string GetDefaultAuthority(const grpc_core::URI& uri) const override;
    grpc_core::OrphanablePtr<grpc_core::Resolver> CreateResolver(
        grpc_core::ResolverArgs args
    ) const override;
};

absl::string_view NodeResolverFactory::scheme() const {
    return "node";
}

bool NodeResolverFactory::IsValidUri(const grpc_core::URI& uri) const {
    return uri.scheme() == scheme();
}

std::string NodeResolverFactory::GetDefaultAuthority(const grpc_core::URI& uri) const {
    return "node-p2p";
}

grpc_core::OrphanablePtr<grpc_core::Resolver> NodeResolverFactory::CreateResolver(
    grpc_core::ResolverArgs args
) const {

    // 1. Parse URI (node://<hash>)
    auto uri = args.uri;
    std::string node_id = uri.path(); // might need stripping leading '/'
    if (!node_id.empty() && node_id[0] == '/') node_id = node_id.substr(1);

    // 2. Extract Signaling Stub from ChannelArgs
    // We expect the user to pass the stub when creating the channel
    auto stub = getRefCountedArg<kMessageStubArgKey, RefCountedArgPtr<message::Message::Stub>>(
        args.args.ToC().get()
    );

    if (!stub.get()) {
        // Fallback or Error if stub isn't provided
        return nullptr;
    }

    return grpc_core::OrphanablePtr<grpc_core::Resolver>(
        static_cast<grpc_core::Resolver*>(
            new NodeResolver(std::move(node_id), std::move(stub), std::move(args))
        )
    );
}

void RegisterNodeResolver() {
  grpc_core::CoreConfiguration::RegisterBuilder(
        // 1st Arg: The Scope.
        // kPersistent means this factory is registered once globally for the lifetime of the process.
        grpc_core::CoreConfiguration::BuilderScope::kPersistent,

        // 2nd Arg: The Lambda (AnyInvocable)
        [](grpc_core::CoreConfiguration::Builder* builder) {
            builder->resolver_registry()->RegisterResolverFactory(
                std::make_unique<NodeResolverFactory>());
        },

        // 3rd Arg: SourceLocation (whence)
        grpc_core::SourceLocation(__FILE__, __LINE__)
    );
}
