#pragma once

#include <mutex>
#include <unordered_map>
#include <atomic>
#include <grpc/event_engine/event_engine.h>

#include <net.pb.h>
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

    class HolePunchListener : public grpc_exp::EventEngine::Listener {
        AcceptCallback on_accept_;
        std::unique_ptr<grpc_exp::MemoryAllocatorFactory> allocator_factory_;
        std::function<void()> on_destroy_; // NEW: Cleanup callback

        public:
        HolePunchListener(
            AcceptCallback on_accept,
            std::unique_ptr<grpc_exp::MemoryAllocatorFactory> factory,
            std::function<void()> on_destroy) // Pass it in the constructor
            : on_accept_(std::move(on_accept)),
            allocator_factory_(std::move(factory)),
            on_destroy_(std::move(on_destroy)) {}

        ~HolePunchListener() override {
            // When gRPC destroys this listener, tell the EventEngine to drop the pointer!
            if (on_destroy_) {
                on_destroy_();
            }
        }

        absl::StatusOr<int> Bind(const grpc_exp::EventEngine::ResolvedAddress& addr) override { return 0; }
        absl::Status Start() override { return absl::OkStatus(); }

        void InjectHolePunchedConnection(std::unique_ptr<grpc_exp::EventEngine::Endpoint> ep) {
            on_accept_(std::move(ep), allocator_factory_->CreateMemoryAllocator("hole_punch_conn"));
        }
    };

    std::shared_ptr<grpc_exp::EventEngine> default_engine_;
    std::shared_ptr<PollEngine> poller_;
    SSL_CTX *ssl_ctx_; // Assuming this behaves like shared_ptr or uniquely owned by Engine

    std::shared_ptr<ConnectionTracker> tracker_ = std::make_shared<ConnectionTracker>();
    std::atomic<intptr_t> next_connection_id_{1};

    // We use a raw pointer here because gRPC's core takes exclusive ownership (unique_ptr)
    // of the Listener. We just keep a weak handle to it so we can inject connections.
    HolePunchListener* active_listener_ = nullptr;
    std::mutex listener_mu_;

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

    // 2. Add a public method your RPC handlers can call!
    absl::StatusOr<std::unique_ptr<grpc_exp::EventEngine::Listener>> CreateListener(
        grpc_exp::EventEngine::Listener::AcceptCallback on_accept,
        absl::AnyInvocable<void(absl::Status)> on_shutdown,
        const grpc_exp::EndpointConfig& config,
        std::unique_ptr<grpc_exp::MemoryAllocatorFactory> memory_allocator_factory
    ) override;
    void AcceptIncomingHolePunch(std::unique_ptr<grpc_exp::EventEngine::Endpoint> ep);
};
