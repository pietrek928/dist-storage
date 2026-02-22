#include "resolver.h"


#include <vector>

#include <grpc/support/log.h>
#include <core/util/uri.h>
#include <core/lib/address_utils/parse_address.h>
#include <core/config/core_configuration.h>

#include <net.pb.h>
#include "ref_counted_arg.h"


class HolePunchArg: public grpc_core::RefCounted<HolePunchArg> {
    public:
    net::HolePunchParameters params;
};

NodeResolver::NodeResolver(
    std::string node_id,
    grpc_core::RefCountedPtr<RefCountedArgPtr<message::Message::Stub>> stub,
    grpc_core::ResolverArgs args
)   : node_id_(std::move(node_id)),
      sig_stub_(std::move(stub)),
      work_serializer_(std::move(args.work_serializer)),
      result_handler_(std::move(args.result_handler)),
      channel_args_(std::move(args.args)) {}

void NodeResolver::StartLocked() {
    if (resolving_) return;
    resolving_ = true;
    StartHolePunching();
}

void NodeResolver::StartHolePunching() {
    hole_punch_context_.Clear();

    // 2. Prepare Hole Punch Request
    // You might populate this based on data from 'negotiation_resp_'
    hole_punch_req_.set_target_node_id(node_id_);

    // Construct the HolePunchParameters to send to the signaling server (if your protocol requires it)
    // Or, we might just be asking the signaling server to coordinate it.
    // Here I assume we send a specific HolePunch RPC.

    auto* hp_params = hole_punch_req_.mutable_hole_punch_params(); // Assuming your SignalingRequest has this field
    hp_params->set_start_time_ms(123456789); // Example: syncing time
    hp_params->set_listen_first(true);       // Determine role
    // ... populate src/dst ips from negotiation_resp_ if needed

    // We also prepare local C++ hole punch params
    final_hole_punch_params_.Clear();

    sig_stub_->get()->async()->Send(&hole_punch_context_, &hole_punch_req_, &hole_punch_resp_,
        [this](grpc::Status status) {
            work_serializer_->Run([this, status]() {
                OnHolePunchingDone(status);
                Unref(); // Release Ref from StartNegotiation (carried over)
            }, DEBUG_LOCATION);
        });
}

void NodeResolver::OnHolePunchingDone(grpc::Status status) {
    if (!status.ok()) {
        resolving_ = false;
        result_handler_->ReportResult(grpc_core::Resolver::Result::FromStatus(
            absl::UnavailableError("Hole Punch Signaling Failed: " + status.error_message())));
        return;
    }

    // Merge any data from hole_punch_resp_ into our final params if needed
    // e.g. final_hole_punch_params_->set_connect_sec_start(hole_punch_resp_.start_time());

    FinishResolution();
}

void NodeResolver::FinishResolution() {
    resolving_ = false;
    auto hole_punch_arg = new HolePunchArg();

    // 1. Create Dummy Address (EventEngine handles the real IP logic)
    grpc_resolved_address addr;
    memset(&addr, 0, sizeof(addr));
    struct sockaddr_in* sin = (struct sockaddr_in*)addr.addr;
    sin->sin_family = AF_INET;
    addr.len = sizeof(struct sockaddr_in);

    // // 2. Pack Data into ChannelArgs
    // // A. The Negotiation Response (Basic Peer Info)
    // auto neg_response_ptr = std::make_shared<SignalingResponse>(negotiation_resp_);
    // grpc_arg neg_arg = MakeCppArg<SignalingResponse>(kSignalingResponseArgKey, neg_response_ptr);

    // grpc_core::ChannelArgs result_args = channel_args_.Set(neg_arg);

    // B. The Hole Punch Parameters
    grpc_arg hp_arg = makeRefCountedArg<kHolePunchParamsArgKey>(hole_punch_arg->Ref());
    grpc_core::ChannelArgs result_args = channel_args_.Set(hp_arg);

    // 3. Create Result
    std::vector<grpc_core::ServerAddress> addresses;
    addresses.emplace_back(addr, result_args);

    grpc_core::Resolver::Result result;
    result.addresses = std::move(addresses);
    result.service_config = absl::OkStatus();

    result_handler_->ReportResult(std::move(result));
}

void NodeResolver::ShutdownLocked() {
    // negotiation_context_.TryCancel();
    hole_punch_context_.TryCancel();
    resolving_ = false;
}

void NodeResolver::RequestReresolutionLocked() {
    StartLocked();
}

void NodeResolver::ResetBackoffLocked() {
    // No-op for this custom resolver
}
