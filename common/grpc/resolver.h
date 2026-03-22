#pragma once

#include <string>
#include <memory>

#include <grpcpp/grpcpp.h>
#include <core/resolver/resolver.h>
#include <core/resolver/resolver_factory.h>
#include <core/util/work_serializer.h>
#include <core/util/debug_location.h>
#include <core/util/ref_counted.h>
#include <core/util/ref_counted_ptr.h>
#include <core/util/orphanable.h>
#include <core/lib/channel/channel_args.h>

#include <message.pb.h>
#include <message.grpc.pb.h>
#include <peer/store.h>
#include <utils/unique_fd.h>
#include <net/tcpv4.h>

#include "ref_counted_arg.h"


class NegotiatedHolePunchArg : public grpc_core::RefCounted<NegotiatedHolePunchArg> {
public:
    unique_fd reserved_fd;
    net::HolePunchParameters params;

    // 1. Tell gRPC the internal string key for this object
    static absl::string_view ChannelArgName() {
        return "grpc.custom.hole_punch_params";
    }

    // 2. Tell gRPC how to compare two of these objects for Subchannel pooling.
    // Comparing 'this' to '&other' means every time you create a new HolePunchArg,
    // gRPC treats it as a unique connection request.
    int Cmp(const NegotiatedHolePunchArg& other) const {
        return grpc_core::QsortCompare(this, &other);
    }
};

class NodeResolver : public grpc_core::Resolver {
public:
    static constexpr char kHolePunchParamsArgKey[] = "grpc.custom.hole_punch_params";

    NodeResolver(
        const std::string &node_id,
        grpc_core::RefCountedPtr<RefCountedArgPtr<message::Message::Stub>> stub,
        grpc_core::ResolverArgs args,
        std::shared_ptr<AuthStoreStore> auth_store
    );

    void StartLocked() override;
    void ShutdownLocked() override;
    void RequestReresolutionLocked() override;
    void ResetBackoffLocked() override;

private:
    // Workflow Steps
    // void StartNegotiation();
    // void OnNegotiationDone(grpc::Status status);

    void StartHolePunching();
    void OnHolePunchingDone(grpc::Status status);

    void FinishResolution();

    // Data
    std::string node_id;
    grpc_core::RefCountedPtr<RefCountedArgPtr<message::Message::Stub>> sig_stub_;
    std::shared_ptr<grpc_core::WorkSerializer> work_serializer_;
    std::unique_ptr<grpc_core::Resolver::ResultHandler> result_handler_;
    grpc_core::ChannelArgs channel_args_;
    std::optional<ReservedSocket> current_port_reservation_;

    // Contexts & Responses (Member variables to keep them alive during async calls)
    std::optional<grpc::ClientContext> hole_punch_context_;

    message::SignedMessage hole_punch_req_;
    message::SendMessageResponse hole_punch_resp_;

    bool resolving_ = false;

    std::shared_ptr<AuthStoreStore> auth_store;
};
