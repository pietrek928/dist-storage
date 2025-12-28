#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/endpoint_config.h>


namespace grpc_exp = grpc_event_engine::experimental;

class HolePunchEventEngine : public grpc_exp::EventEngine {
    std::shared_ptr<EventEngine> default_engine;

public:
    HolePunchEventEngine() : default_engine(grpc_exp::CreateEventEngine()) {}
    // HolePunchEventEngine(std::shared_ptr<PollEngine> poller)
    //     : poller_(std::move(poller)) {}

    // Delegate Run to the default gRPC thread pool
    void Run(absl::AnyInvocable<void()> task) override {
        default_engine->Run(std::move(task));
    }

    // Delegate Timers so keep-alives and timeouts work
    TaskHandle RunAfter(Duration when, absl::AnyInvocable<void()> task) override {
        return default_engine->RunAfter(when, std::move(task));
    }

    bool Cancel(TaskHandle handle) override {
        return default_engine->Cancel(handle);
    }

    // --- ENDPOINT FACTORY ---
    // This is called when gRPC acts as a CLIENT
    ConnectionHandle Connect(OnConnectCallback on_connect,
                             const ResolvedAddress& addr,
                             const grpc_exp::EndpointConfig& args,
                             grpc_exp::MemoryAllocator memory_allocator,
                             Duration timeout) override {
        // 1. Start your Hole Punching logic here
        // 2. Once connected and SSL-handshaked:
        //    auto ep = std::make_unique<ManualSslEndpoint>(fd, ssl, poller_.get(), shared_from_this());
        //    on_connect(std::move(ep));
        return ConnectionHandle::kInvalid;
    }

    // --- LISTENER FACTORY ---
    // This is called when gRPC acts as a SERVER
    absl::StatusOr<std::unique_ptr<Listener>> CreateListener(
        Listener::AcceptCallback on_accept,
        absl::AnyInvocable<void(absl::Status)> on_shutdown,
        const EndpointConfig& config,
        MemoryAllocatorFactory memory_allocator_factory) override {

        return std::make_unique<HolePunchListener>(
            std::move(on_accept), poller_, shared_from_this());
    }

    // Other required overrides (usually return false or kInvalid for custom logic)
    bool IsInEventThread() override { return false; }

private:
    std::shared_ptr<PollEngine> poller_;
};
