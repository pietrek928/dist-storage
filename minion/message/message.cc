#include <netinet/in.h>
#include <random>

#include <grpc/callback.h>
#include <grpc/engine.h>

#include <absl/log/log.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/server_builder.h>

#include <crypto/ssl.h>
#include <google/protobuf/empty.pb.h>
#include <message.grpc.pb.h>
#include <message.pb.h>
#include <net/tcpv4.h>
#include <peer/message.h>
#include <peer/store.h>
#include <utils/poll_engine.h>

#include "message_stream.h"
#include "stream_router.h"

class MessageSendHandler : public GRPCBasicHandler<
    message::Message::AsyncService,
    message::SignedMessage,
    message::SendMessageResponse,
    MessageSendHandler
> {
public:
    MessageSendHandler(
        message::Message::AsyncService* async_service,
        HolePunchEventEngine* hole_punch_engine,
        std::shared_ptr<AuthStore> auth_store
    )
        : GRPCBasicHandler(async_service),
          hole_punch_engine_(hole_punch_engine),
          auth_store_(std::move(auth_store)) {}

    void bind(grpc::ServerCompletionQueue* cq) override {
        service->RequestSend(&ctx, &request, &responder, cq, cq, this);
    }

    void handle_request() override {
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
            fill_src_ipv4_from_socket(params.mutable_src()->mutable_ipv4(), reservation.fd);
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

private:
    HolePunchEventEngine* hole_punch_engine_;
    std::shared_ptr<AuthStore> auth_store_;
};

class TellMyAddrHandler : public GRPCBasicHandler<
    message::Message::AsyncService,
    google::protobuf::Empty,
    net::IPAddr,
    TellMyAddrHandler
> {
public:
    explicit TellMyAddrHandler(message::Message::AsyncService* async_service)
        : GRPCBasicHandler(async_service) {}

    void bind(grpc::ServerCompletionQueue* cq) override {
        service->RequestTellMyAddr(&ctx, &request, &responder, cq, cq, this);
    }

    void handle_request() override {
        finish_status_ = grpc::Status::OK;
        if (!grpc_peer_uri_to_net_ip_addr(ctx.peer(), &response)) {
            LOG(WARNING) << "TellMyAddr: could not parse peer URI (IPv4 only): " << ctx.peer();
            finish_status_ = grpc::Status(
                grpc::INVALID_ARGUMENT,
                "could not parse peer address (expected ipv4:host:port)"
            );
        }
    }

    grpc::Status finish_grpc_status() override {
        return finish_status_;
    }

private:
    grpc::Status finish_status_{grpc::Status::OK};
};

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    const char* listen = "0.0.0.0:50052";

    SSL_library_init();
    SSL_CTX_ptr ssl_ctx(SSL_CTX_new(TLS_server_method()));
    if (!ssl_ctx) {
        LOG(ERROR) << "SSL_CTX_new failed";
        return 1;
    }
    prepare_ssl_for_grpc(ssl_ctx.get());

    auto poller = std::make_shared<PollEngine>();
    HolePunchEventEngine hole_punch_engine(poller, ssl_ctx.get());
    auto auth_store = std::make_shared<AuthStore>();
    auto stream_router = std::make_shared<MessageStreamRouter>(auth_store);

    message::Message::AsyncService async_service;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(listen, grpc::InsecureServerCredentials());
    builder.RegisterService(&async_service);
    std::unique_ptr<grpc::ServerCompletionQueue> cq = builder.AddCompletionQueue();
    std::unique_ptr<grpc::Server> server = builder.BuildAndStart();

    auto* send_initial = new MessageSendHandler(&async_service, &hole_punch_engine, auth_store);
    send_initial->process(cq.get(), true);

    auto* addr_initial = new TellMyAddrHandler(&async_service);
    addr_initial->process(cq.get(), true);

    start_message_stream_acceptor(&async_service, cq.get(), auth_store, stream_router);

    void* tag;
    bool ok = true;
    while (cq->Next(&tag, &ok)) {
        static_cast<GRPCHandler*>(tag)->process(cq.get(), ok);
    }

    return 0;
}
