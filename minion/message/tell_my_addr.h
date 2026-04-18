#pragma once

#include <grpc/callback.h>
#include <google/protobuf/empty.pb.h>
#include <message.grpc.pb.h>
#include <message.pb.h>

/// Async unary `TellMyAddr`: maps gRPC peer string to `net::IPAddr`.
class TellMyAddrHandler : public GRPCBasicHandler<
    TellMyAddrHandler,
    message::Message::AsyncService,
    google::protobuf::Empty,
    net::IPAddr
> {
public:
    explicit TellMyAddrHandler(message::Message::AsyncService* async_service);

    void bind(grpc::ServerCompletionQueue* cq) override;
    void handle_request() override;
    grpc::Status finish_grpc_status() override;

private:
    grpc::Status finish_status_{grpc::Status::OK};
};
