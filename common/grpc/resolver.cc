#include "resolver.h"


#include <netinet/in.h>
#include <vector>

#include <absl/log/log.h>
#include <core/util/uri.h>
#include <core/lib/address_utils/parse_address.h>
#include <core/config/core_configuration.h>

#include <net.pb.h>
#include <peer/message.h>
#include "ref_counted_arg.h"


NodeResolver::NodeResolver(
    const std::string &node_id,
    grpc_core::RefCountedPtr<RefCountedArgPtr<message::Message::Stub>> stub,
    grpc_core::ResolverArgs args,
    std::shared_ptr<AuthStoreStore> auth_store
)   : node_id(node_id),
      sig_stub_(std::move(stub)),
      work_serializer_(std::move(args.work_serializer)),
      result_handler_(std::move(args.result_handler)),
      channel_args_(std::move(args.args)),
      auth_store(auth_store) {}

void NodeResolver::StartHolePunching() {
    hole_punch_context_.emplace();

    // 1. Only reserve a new port if we don't already have one!
    if (!current_port_reservation_ || !current_port_reservation_->fd.valid()) {
        std::random_device rd;
        current_port_reservation_ = tcpv4_bind_random_port(rd, 12345, 23456, INADDR_ANY);

        if (!current_port_reservation_->fd.valid()) {
            LOG(ERROR) << "Failed to reserve a local port for hole punching in range 12345-23456";
            resolving_ = false;

            grpc_core::Resolver::Result result;
            result.addresses = absl::ResourceExhaustedError("No ports available in configured range");
            result_handler_->ReportResult(std::move(result));
            return;
        }
    }

    // 2. Prepare Hole Punch Request safely
    message::MessageData data;
    auto* params = data.mutable_hole_punch();
    // TODO: self ip connection data for peer
    // TODO: params->set_src_port(port_reservation->port);
    params->set_start_time_ms(1234); // TODO: from timestamp
    params->set_connect_count(4); // TODO: values from some config
    params->set_connect_sec_start(5);
    params->set_connect_sec_max(6);
    params->set_connect_sec_scale(2.5);

    hole_punch_req_ = auth_store->sign_message(data, node_id);

    // 3. Setup Async Lifecycles
    auto self = Ref(DEBUG_LOCATION, "StartHolePunching");

    // 4. Fire the Network Request
    // LOOK HOW CLEAN THIS LAMBDA IS NOW! No sockets, no shared_ptrs.
    sig_stub_->get()->async()->Send(
        &hole_punch_context_.value(),
        &hole_punch_req_,
        &hole_punch_resp_,
        [this, self = std::move(self)](grpc::Status status) mutable {
            work_serializer_->Run(
                [this, status, self = std::move(self)]() mutable {

                    // We just call the function. The socket is already in 'this'.
                    OnHolePunchingDone(status);

                }, DEBUG_LOCATION);
        }
    );
}

void NodeResolver::OnHolePunchingDone(grpc::Status status) {
    if (!status.ok()) {
        resolving_ = false;

        LOG(ERROR) << "Hole punch signaling RPC failed with code "
                   << status.error_code() << ": " << status.error_message();

        current_port_reservation_.reset();

        grpc_core::Resolver::Result result;
        result.addresses = absl::UnavailableError(
            "Hole Punch Signaling Failed: " + status.error_message());
        result_handler_->ReportResult(std::move(result));
        return;
    }

    // Merge any data from hole_punch_resp_ into our final params if needed
    // e.g. final_hole_punch_params_->set_connect_sec_start(hole_punch_resp_.start_time());

    FinishResolution();
}

void NodeResolver::FinishResolution() {
    resolving_ = false;

    // 1. Create the arg safely (Ref count starts at 1, automatically managed)
    auto hole_punch_arg = grpc_core::MakeRefCounted<NegotiatedHolePunchArg>();
    // Steal the socket out of the class member and move it to the args
    hole_punch_arg->reserved_fd = std::move(current_port_reservation_->fd);
    // Clear the reservation so we know we need a new one if the channel drops
    current_port_reservation_.reset();

    // NOTE: You must std::move your reserved socket into the arg!
    // hole_punch_arg->reserved_fd = std::move(my_reserved_socket);
    // hole_punch_arg->params = final_hole_punch_params_;

    // 2. Create Dummy Address
    // (EventEngine will ignore this and use the HolePunchArg instead)
    grpc_resolved_address addr;
    memset(&addr, 0, sizeof(addr));
    struct sockaddr_in* sin = (struct sockaddr_in*)addr.addr;
    sin->sin_family = AF_INET;
    addr.len = sizeof(struct sockaddr_in);

    // 3. Safely Pack the C++ Object into ChannelArgs
    // SetObject automatically handles the Ref(), Unref(), and Cmp functions!
    grpc_core::ChannelArgs result_args = channel_args_.SetObject(std::move(hole_punch_arg));

    // 4. Attach args to the Address
    std::vector<grpc_core::ServerAddress> addresses;
    addresses.emplace_back(addr, result_args);

    // 5. Create and Report Result
    grpc_core::Resolver::Result result;
    result.addresses = std::move(addresses);
    result.service_config = absl::OkStatus();

    result_handler_->ReportResult(std::move(result));
}

void NodeResolver::ShutdownLocked() {
    resolving_ = false;

    // Check if the pointer actually holds a context before canceling
    if (hole_punch_context_) {
        hole_punch_context_->TryCancel();
    }
}

void NodeResolver::StartLocked() {
    if (resolving_) return;
    resolving_ = true;

    // 1. You must reserve a NEW port for this resolution attempt,
    // because the previous one was std::move'd to the EventEngine.
    std::random_device rd;
    auto port_reservation = tcpv4_bind_random_port(
        // TODO: get those ports from config
        rd, 12345, 23456, INADDR_ANY
    );
    if (!port_reservation.fd.valid()) {
        // Handle critical failure (e.g., report Unavailable error to result_handler_)
        resolving_ = false;
        return;
    }

    // Store the new reservation in your class member to be used by StartHolePunching
    current_port_reservation_ = std::move(port_reservation);

    // 2. Now begin the networking pipeline
    StartHolePunching();
}

void NodeResolver::RequestReresolutionLocked() {
    // This correctly re-triggers StartLocked(), which will now safely grab a fresh port.
    StartLocked();
}

void NodeResolver::ResetBackoffLocked() {
    // No-op for this custom resolver
}
