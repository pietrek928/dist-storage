#include <grpc/callback.h>
#include <grpc/engine.h>

#include <absl/log/log.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/server_builder.h>

#include <crypto/ssl.h>
#include <message.grpc.pb.h>
#include <peer/store.h>
#include <utils/poll_engine.h>

#include "message_send.h"
#include "message_stream.h"
#include "stream_router.h"
#include "tell_my_addr.h"

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
