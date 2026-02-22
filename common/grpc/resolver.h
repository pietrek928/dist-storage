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

#include "ref_counted_arg.h"


class NodeResolver : public grpc_core::Resolver {
public:
    static constexpr char kHolePunchParamsArgKey[] = "grpc.custom.hole_punch_params";

    NodeResolver(
        std::string node_id,
        grpc_core::RefCountedPtr<RefCountedArgPtr<message::Message::Stub>> stub,
        grpc_core::ResolverArgs args
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
    std::string node_id_;
    grpc_core::RefCountedPtr<RefCountedArgPtr<message::Message::Stub>> sig_stub_;
    std::shared_ptr<grpc_core::WorkSerializer> work_serializer_;
    std::unique_ptr<grpc_core::Resolver::ResultHandler> result_handler_;
    grpc_core::ChannelArgs channel_args_;

    // Contexts & Responses (Member variables to keep them alive during async calls)
    grpc::ClientContext hole_punch_context_;

    message::SignedMessage hole_punch_req_;
    message::SendMessageResponse hole_punch_resp_;

    bool resolving_ = false;
};
