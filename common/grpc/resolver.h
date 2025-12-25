#include <grpcpp/grpcpp.h>
#include "directory.grpc.pb.h" // Your discovery service proto


class NodeResolver : public grpc_core::Resolver {
public:
    NodeResolver(grpc_core::ResolverArgs args, std::shared_ptr<grpc::Channel> dir_channel)
        : resolver_args_(std::move(args)),
          stub_(DirectoryService::NewStub(dir_channel)) {}

    void StartLocked() override {
        // Extract hash from URI: node://<hash>
        std::string hash = std::string(resolver_args_.uri.path());

        // 1. Prepare the Async Call
        auto* call = new AsyncResolveCall();
        call->request.set_node_hash(hash);

        // 2. Initiate the RPC (This does NOT block)
        stub_->async()->ResolveNode(&call->context, &call->request, &call->response,
            [this, call](grpc::Status status) {
                // This callback runs when the Directory Service responds
                OnLookupComplete(status, call);
            });
    }

    void ShutdownLocked() override { /* Cancel pending calls if necessary */ }

private:
    struct AsyncResolveCall {
        grpc::ClientContext context;
        ResolveRequest request;
        ResolveResponse response;
    };

    void OnLookupComplete(grpc::Status status, AsyncResolveCall* call) {
        if (status.ok()) {
            // 3. Convert response (IP/Port) to gRPC addresses
            grpc_resolved_address addr;
            ParseAddress(call->response.address(), &addr);

            // 4. Push results back to the channel
            grpc_core::Resolver::Result result;
            result.addresses = std::make_unique<grpc_core::ServerAddressList>();
            result.addresses->emplace_back(addr, nullptr);

            // Inform gRPC we found the node!
            resolver_args_.result_handler->OnResult(std::move(result));
        } else {
            resolver_args_.result_handler->OnError(absl::UnavailableError("Node not found"));
        }
        delete call;
    }

    grpc_core::ResolverArgs resolver_args_;
    std::unique_ptr<DirectoryService::Stub> stub_;
};