#include <grpcpp/grpcpp.h>
#include <grpc/event_engine/event_engine.h>

// Note: These are internal gRPC headers required for custom resolvers
#include <src/core/lib/resolver/resolver.h>
#include <src/core/lib/resolver/resolver_factory.h>
#include <src/core/lib/iomgr/work_serializer.h>

// Your generated protobuf headers
#include "signaling.grpc.pb.h"

using namespace grpc_event_engine::experimental;

// Unique key for the attribute map
static const char* kSignalingResponseKey = "signaling_response_proto";

class NodeResolver : public grpc_core::Resolver,
                     public std::enable_shared_from_this<NodeResolver> {
public:
    NodeResolver(std::string node_hash,
                 std::shared_ptr<Signaling::Stub> sig_stub,
                 grpc_core::ResolverArgs args)
        : node_hash_(std::move(node_hash)),
          sig_stub_(std::move(sig_stub)),
          work_serializer_(std::move(args.work_serializer)),
          result_handler_(std::move(args.result_handler)) {

        // Initialize the request with the target hash immediately
        sig_request_.set_target_node_id(node_hash_);
    }

    // Called by gRPC to start or re-resolve
    void StartLocked() override {
        if (resolving_) return;
        resolving_ = true;

        // Use a weak_ptr to ensure we don't block destruction if an RPC is in-flight
        std::weak_ptr<NodeResolver> weak_self = shared_from_this();

        // Perform the Async Signaling Call
        sig_stub_->async()->Negotiate(&sig_context_, &sig_request_, &sig_response_,
            [weak_self](grpc::Status status) {
                auto self = weak_self.lock();
                if (!self) return;

                // CRITICAL: We are on a gRPC internal thread.
                // We MUST move back to the WorkSerializer to touch Resolver state.
                self->work_serializer_->Run([self, status]() {
                    self->OnNegotiationDone(status);
                }, DEBUG_LOCATION);
            });
    }

    void ShutdownLocked() override {
        if (resolving_) {
            sig_context_.TryCancel();
            resolving_ = false;
        }
    }

private:
    void OnNegotiationDone(grpc::Status status) {
        if (!resolving_) return;
        resolving_ = false;

        if (!status.ok()) {
            // Report error to gRPC so it can retry with backoff
            result_handler_->ReportResult(grpc_core::Resolver::Result::FromStatus(
                absl::InternalError("Signaling failed: " + status.error_message())));
            return;
        }

        // 1. Prepare the Result object for the Load Balancer
        grpc_core::Resolver::Result result;

        // 2. Wrap the Protobuf Response in a shared_ptr to pass as an attribute
        auto response_ptr = std::make_shared<SignalingResponse>(std::move(sig_response_));

        // 3. Create a dummy address used as a key for connection pooling
        auto dummy_addr = CreateDummyAddress(node_hash_);

        // 4. Attach attributes to the address
        grpc_core::AddressReceiverAttributes attributes;
        attributes.Set(kSignalingResponseKey, std::move(response_ptr));

        // Add the address + metadata to the result set
        result.addresses.emplace_back(dummy_addr, std::move(attributes));

        // 5. Hand the result back to gRPC Core
        result_handler_->ReportResult(std::move(result));
    }

    // Generates a unique-looking local address for each node hash
    EventEngine::ResolvedAddress CreateDummyAddress(const std::string& hash) {
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        // Use 127.0.0.1 for all, gRPC distinguishes by attributes too
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(0);
        return EventEngine::ResolvedAddress(reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    }

    std::string node_hash_;
    std::shared_ptr<Signaling::Stub> sig_stub_;
    std::shared_ptr<grpc_core::WorkSerializer> work_serializer_;
    std::unique_ptr<grpc_core::Resolver::ResultHandler> result_handler_;

    grpc::ClientContext sig_context_;
    SignalingRequest sig_request_;
    SignalingResponse sig_response_;
    bool resolving_ = false;
};