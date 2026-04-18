#include "message_send.h"

#include <random>

#include <absl/log/log.h>
#include <grpcpp/grpcpp.h>

#include <grpc/engine.h>
#include <net/tcpv4.h>
#include <peer/message.h>

MessageSendHandler::MessageSendHandler(
    message::Message::AsyncService* async_service,
    HolePunchEventEngine* hole_punch_engine,
    std::shared_ptr<AuthStore> auth_store
)
    : GRPCBasicHandler(async_service),
      hole_punch_engine_(hole_punch_engine),
      auth_store_(std::move(auth_store)) {}

void MessageSendHandler::bind(grpc::ServerCompletionQueue* cq) {
    service->RequestSend(&ctx, &request, &responder, cq, cq, this);
}

void MessageSendHandler::handle_request() {
    message::MessageData data;
    switch (authenticate_signed_message_data(auth_store_.get(), request, &data, "Send")) {
        case SignedPayloadAuthResult::Ok:
            break;
        case SignedPayloadAuthResult::UnknownSender:
        case SignedPayloadAuthResult::BadSignature:
            response.set_status(message::SendMessageStatus::REJECTED);
            return;
        case SignedPayloadAuthResult::BadPayload:
            response.set_status(message::SendMessageStatus::FAILED);
            return;
    }

    if (!data.has_hole_punch()) {
        LOG(WARNING) << "Rejected message: missing hole_punch payload";
        response.set_status(message::SendMessageStatus::REJECTED);
        return;
    }

    if (data.hole_punch().dst().addr_case() != net::IPAddr::kIpv4) {
        LOG(WARNING) << "Hole punch parameters missing IPv4 destination";
        response.set_status(message::SendMessageStatus::FAILED);
        return;
    }

    std::random_device rd;
    ReservedSocket reservation = tcpv4_bind_random_port(rd, 12345, 23456, INADDR_ANY);
    if (!reservation.fd.valid()) {
        LOG(WARNING) << "Failed to reserve local TCP port for hole punch";
        response.set_status(message::SendMessageStatus::FAILED);
        return;
    }

    net::HolePunchParameters params = data.hole_punch();
    try {
        tcpv4_addr_from_socket(params.mutable_src()->mutable_ipv4(), reservation.fd);
    } catch (const std::exception& e) {
        LOG(ERROR) << e.what();
        LOG(WARNING) << "Failed to populate source IPv4 from reserved socket";
        response.set_status(message::SendMessageStatus::FAILED);
        return;
    }

    unique_fd punch_fd = std::move(reservation.fd);
    absl::Status st = hole_punch_engine_->RunSignaledIncomingHolePunch(std::move(punch_fd), params);
    if (!st.ok()) {
        LOG(ERROR) << st;
        LOG(WARNING) << "Incoming hole punch execution failed";
        response.set_status(message::SendMessageStatus::FAILED);
        return;
    }

    response.set_status(message::SendMessageStatus::OK);
}
