#pragma once

#include <memory>

#include <grpcpp/completion_queue.h>
#include <message.grpc.pb.h>

class AuthStore;
class MessageStreamRouter;

void start_message_stream_acceptor(
    message::Message::AsyncService* async_service,
    grpc::ServerCompletionQueue* cq,
    std::shared_ptr<AuthStore> auth_store,
    std::shared_ptr<MessageStreamRouter> router
);
