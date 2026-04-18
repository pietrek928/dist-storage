#include "tell_my_addr.h"

#include <absl/log/log.h>
#include <grpcpp/grpcpp.h>

#include <grpc/addr.h>

TellMyAddrHandler::TellMyAddrHandler(message::Message::AsyncService* async_service)
    : GRPCBasicHandler(async_service) {}

void TellMyAddrHandler::bind(grpc::ServerCompletionQueue* cq) {
    service->RequestTellMyAddr(&ctx, &request, &responder, cq, cq, this);
}

void TellMyAddrHandler::handle_request() {
    finish_status_ = grpc::Status::OK;
    if (!grpc_peer_uri_to_net_ip_addr(ctx.peer(), &response)) {
        LOG(WARNING) << "TellMyAddr: could not parse peer URI: " << ctx.peer();
        finish_status_ = grpc::Status(
            grpc::INVALID_ARGUMENT,
            "could not parse peer address (expected ipv4:host:port or ipv6:[host]:port)"
        );
    }
}

grpc::Status TellMyAddrHandler::finish_grpc_status() { return finish_status_; }
