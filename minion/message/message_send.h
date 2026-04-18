#pragma once

#include <memory>

#include <grpc/callback.h>
#include <message.grpc.pb.h>
#include <message.pb.h>

#include <peer/store.h>

class HolePunchEventEngine;

/// Async unary `Send` handler: signed hole-punch payload validation and execution.
class MessageSendHandler : public GRPCBasicHandler<
    MessageSendHandler,
    message::Message::AsyncService,
    message::SignedMessage,
    message::SendMessageResponse
> {
public:
    MessageSendHandler(
        message::Message::AsyncService* async_service,
        HolePunchEventEngine* hole_punch_engine,
        std::shared_ptr<AuthStore> auth_store
    );

    void bind(grpc::ServerCompletionQueue* cq) override;
    void handle_request() override;

private:
    HolePunchEventEngine* hole_punch_engine_;
    std::shared_ptr<AuthStore> auth_store_;
};
