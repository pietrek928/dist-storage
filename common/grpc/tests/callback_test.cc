#include <gtest/gtest.h>

#include <atomic>
#include <memory>

#include <grpc/callback.h>

namespace {

std::atomic<int> g_live{0};

class CountingHandler : public GRPCHandler {
public:
    std::unique_ptr<CountingHandler> self_;

    CountingHandler() { g_live.fetch_add(1); }
    CountingHandler(const CountingHandler&) { g_live.fetch_add(1); }
    ~CountingHandler() override { g_live.fetch_sub(1); }

    void arm(std::unique_ptr<CountingHandler> self) { self_ = std::move(self); }

    void process(grpc::ServerCompletionQueue* /*cq*/, bool /*running*/) override {
        grpc_defer_handler_destroy(std::unique_ptr<GRPCHandler>(self_.release()));
    }
};

class InnerHandler : public GRPCHandler {
public:
    void process(grpc::ServerCompletionQueue* /*cq*/, bool /*running*/) override {}
};

}  // namespace

TEST(CallbackTest, DeferredDestroyRunsOnFlush) {
    g_live.store(0);
    {
        auto p = std::make_unique<CountingHandler>();
        EXPECT_EQ(g_live.load(), 1);
        grpc_defer_handler_destroy(std::move(p));
        EXPECT_EQ(g_live.load(), 1);
        grpc_run_deferred_handler_destroys();
        EXPECT_EQ(g_live.load(), 0);
    }
}

TEST(CallbackTest, CounterHandlerCountsProcess) {
    InnerHandler inner;
    GRPCCounterHandler counter(&inner);
    counter.process(nullptr, true);
    counter.process(nullptr, false);
    EXPECT_EQ(counter.get(), 2);
    counter.reset();
    EXPECT_EQ(counter.get(), 0);
}

TEST(CallbackTest, PrimeAsyncHandlerDestroysViaDefer) {
    g_live.store(0);
    grpc_prime_async_handler(std::make_unique<CountingHandler>(), nullptr, true);
    EXPECT_EQ(g_live.load(), 0);
}

TEST(CallbackTest, CloneAcceptorSkippedWhenNotRunning) {
    g_live.store(0);
    CountingHandler proto;
    EXPECT_EQ(g_live.load(), 1);
    grpc_async_clone_acceptor(proto, nullptr, false);
    EXPECT_EQ(g_live.load(), 1);
}

TEST(CallbackTest, CloneAcceptorCreatesAndDestroysWhenRunning) {
    g_live.store(0);
    CountingHandler proto;
    EXPECT_EQ(g_live.load(), 1);
    grpc_async_clone_acceptor(proto, nullptr, true);
    EXPECT_EQ(g_live.load(), 1);
}
