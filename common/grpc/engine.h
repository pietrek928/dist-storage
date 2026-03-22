#pragma once

#include <mutex>
#include <unordered_map>
#include <atomic>
#include <grpc/event_engine/event_engine.h>

#include <crypto/ssl.h>
#include <utils/poll_engine.h>

namespace grpc_exp = grpc_event_engine::experimental;


class HolePunchEventEngine : public grpc_exp::EventEngine {
    // Lock-Free Shared Cancellation State ---
    struct ConnectionTracker {
        std::mutex mu;
        std::unordered_map<intptr_t, std::shared_ptr<std::atomic<bool>>> active_flags;
    };

    // The RAII Scope Guard!
    // No matter how this lambda exits (return or exception),
    // the destructor of this local struct will cleanly erase the tracker.
    struct CleanupGuard {
        std::shared_ptr<ConnectionTracker> t;
        intptr_t id;
        ~CleanupGuard() {
            std::lock_guard<std::mutex> lock(t->mu);
            t->active_flags.erase(id);
        }
    };

    std::shared_ptr<grpc_exp::EventEngine> default_engine_;
    std::shared_ptr<PollEngine> poller_;
    SSL_CTX *ssl_ctx_; // Assuming this behaves like shared_ptr or uniquely owned by Engine

    std::shared_ptr<ConnectionTracker> tracker_ = std::make_shared<ConnectionTracker>();
    std::atomic<intptr_t> next_connection_id_{1};

public:
    HolePunchEventEngine(std::shared_ptr<PollEngine> poller, SSL_CTX* ssl_ctx);
    ~HolePunchEventEngine() override = default;

    // --- DELEGATED METHODS ---
    void Run(absl::AnyInvocable<void()> task) override;
    TaskHandle RunAfter(Duration when, absl::AnyInvocable<void()> task) override;
    bool Cancel(TaskHandle handle) override;
    bool IsWorkerThread() override;
    absl::StatusOr<std::unique_ptr<grpc_exp::EventEngine::DNSResolver>> GetDNSResolver(
        const grpc_exp::EventEngine::DNSResolver::ResolverOptions& options
    ) override;

    // --- Lock-Free CancelConnect ---
    bool CancelConnect(ConnectionHandle handle) override;

    // --- ENDPOINT FACTORY ---
    ConnectionHandle Connect(
        OnConnectCallback on_connect,
        const ResolvedAddress& addr,
        const grpc_exp::EndpointConfig& args,
        grpc_exp::MemoryAllocator memory_allocator,
        Duration timeout
    ) override;
};
