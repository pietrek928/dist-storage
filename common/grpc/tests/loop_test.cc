#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include <grpcpp/grpcpp.h>
#include <grpcpp/server_builder.h>
#include <google/protobuf/empty.pb.h>
#include <message.grpc.pb.h>
#include <net.pb.h>

#include <grpc/callback.h>
#include <grpc/loop.h>

namespace {

std::atomic<int> g_not_ok_count{0};
std::atomic<int> g_handler_destroyed{0};

class LoopTestHandler : public GRPCBasicHandler<
                            LoopTestHandler,
                            message::Message::AsyncService,
                            google::protobuf::Empty,
                            net::IPAddr> {
public:
    using GRPCBasicHandler::GRPCBasicHandler;
    LoopTestHandler(const LoopTestHandler& other) : GRPCBasicHandler(other) {}

    ~LoopTestHandler() override { g_handler_destroyed.fetch_add(1); }

    void bind(grpc::ServerCompletionQueue* cq) override {
        service->RequestTellMyAddr(&ctx, &request, &responder, cq, cq, this);
    }

    void handle_request() override {
        response.mutable_ipv4()->mutable_addr()->assign(4, '\x7f');
        response.mutable_ipv4()->set_port(1);
    }

    void process(grpc::ServerCompletionQueue* cq, bool running) override {
        if (!running) {
            g_not_ok_count.fetch_add(1);
        }
        GRPCBasicHandler::process(cq, running);
    }
};

}  // namespace

TEST(LoopTest, PassesOkFalseOnShutdownAndDestroysHandlers) {
    g_not_ok_count.store(0);
    g_handler_destroyed.store(0);

    message::Message::AsyncService service;
    grpc::ServerBuilder builder;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<grpc::ServerCompletionQueue> cq = builder.AddCompletionQueue();
    std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
    ASSERT_NE(server, nullptr);

    grpc_prime_async_handler(std::make_unique<LoopTestHandler>(&service), cq.get(), true);

    std::thread loop_thread([&]() { grpc_loop(cq.get()); });

    server->Shutdown();
    cq->Shutdown();
    loop_thread.join();

    EXPECT_GE(g_not_ok_count.load(), 1);
    EXPECT_GE(g_handler_destroyed.load(), 1);
}
