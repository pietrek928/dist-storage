#pragma once

#include <grpcpp/grpcpp.h>

#include <core/util/ref_counted_ptr.h>
#include <core/util/debug_location.h>
#include <core/resolver/resolver.h>
#include <core/lib/iomgr/work_serializer.h>

#include "net.grpc.pb.h"


// Unique key to pass signaling data to the EventEngine
static const char* kSignalingDataKey = "signaling_response_data";

class NodeResolver : public grpc_core::Resolver {
public:
    NodeResolver(std::string node_hash,
                 std::shared_ptr<Signaling::Stub> sig_stub,
                 grpc_core::ResolverArgs args)
        : node_hash_(std::move(node_hash)),
          sig_stub_(std::move(sig_stub)),
          work_serializer_(std::move(args.work_serializer)),
          result_handler_(std::move(args.result_handler)) {}

    void StartLocked() override {
        if (resolving_) return;
        resolving_ = true;

        // Prepare the async signaling request
        auto request = std::make_unique<SignalingRequest>();
        request->set_target_node_id(node_hash_);

        // We use a RefCountedPtr to keep 'this' alive during the async call
        auto self = grpc_core::RefCountedPtr<NodeResolver>(this);

        sig_stub_->async()->Negotiate(&sig_context_, request.get(), &sig_response_,
            [self, req = std::move(request)](grpc::Status status) {
                // Return to the WorkSerializer to update state
                self->work_serializer_->Run([self, status]() {
                    self->OnSignalingDone(status);
                }, DEBUG_LOCATION);
            });
    }

    void RequestReresolutionLocked() override {
        StartLocked();
    }

    void ResetBackoffLocked() override {
        // Implementation for backoff reset if using a reconnection loop
    }

    void ShutdownLocked() override {
        if (resolving_) {
            sig_context_.TryCancel();
            resolving_ = false;
        }
    }

private:
    void OnSignalingDone(grpc::Status status) {
        if (!resolving_) return;
        resolving_ = false;

        if (!status.ok()) {
            result_handler_->ReportResult(grpc_core::Resolver::Result::FromStatus(
                absl::UnavailableError("Signaling failed: " + status.error_message())));
            return;
        }

        // 1. Create the Result object
        grpc_core::Resolver::Result result;

        // 2. Package the signaling response into an attribute
        // In 1.75.1, we store this in an AttributeMap
        auto response_copy = std::make_shared<SignalingResponse>(std::move(sig_response_));

        // 3. Create a dummy address (127.0.0.1:0)
        // The EventEngine will ignore this IP and use the signaling data instead
        grpc_resolved_address dummy_addr;
        memset(&dummy_addr, 0, sizeof(dummy_addr));
        // ... (standard sockaddr_in setup for 127.0.0.1)

        // 4. Attach metadata to the address
        grpc_core::AttributeContainer attrs;
        attrs.Set(kSignalingDataKey, std::move(response_copy));

        // Add to the address list
        result.addresses.emplace_back(dummy_addr, std::move(attrs));

        // 5. Hand off to the Load Balancer
        result_handler_->ReportResult(std::move(result));
    }

    std::string node_hash_;
    std::shared_ptr<Signaling::Stub> sig_stub_;
    std::shared_ptr<grpc_core::WorkSerializer> work_serializer_;
    std::unique_ptr<grpc_core::Resolver::ResultHandler> result_handler_;

    grpc::ClientContext sig_context_;
    SignalingResponse sig_response_;
    bool resolving_ = false;
};