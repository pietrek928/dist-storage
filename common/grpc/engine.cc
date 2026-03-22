#include "engine.h"

#include <thread>
#include <grpc/event_engine/endpoint_config.h>

#include <net/tcpv4.h>

#include "resolver.h"
#include "ssl_endpoint.h"


HolePunchEventEngine::HolePunchEventEngine(std::shared_ptr<PollEngine> poller, SSL_CTX* ssl_ctx)
    : default_engine_(grpc_exp::CreateEventEngine()),
        poller_(std::move(poller)),
        ssl_ctx_(ssl_ctx) {}

void HolePunchEventEngine::Run(absl::AnyInvocable<void()> task) {
    default_engine_->Run(std::move(task));
}

HolePunchEventEngine::TaskHandle HolePunchEventEngine::RunAfter(
    Duration when, absl::AnyInvocable<void()> task
) {
    return default_engine_->RunAfter(when, std::move(task));
}

bool HolePunchEventEngine::Cancel(TaskHandle handle) {
    return default_engine_->Cancel(handle);
}

bool HolePunchEventEngine::IsWorkerThread() {
    return default_engine_->IsWorkerThread();
}

absl::StatusOr<std::unique_ptr<grpc_exp::EventEngine::DNSResolver>> HolePunchEventEngine::GetDNSResolver(
    const grpc_exp::EventEngine::DNSResolver::ResolverOptions& options
) {
    return default_engine_->GetDNSResolver(options);
}

bool HolePunchEventEngine::CancelConnect(ConnectionHandle handle) {
    std::shared_ptr<std::atomic<bool>> flag;
    {
        std::lock_guard<std::mutex> lock(tracker_->mu);
        auto it = tracker_->active_flags.find(handle.keys[0]);
        if (it != tracker_->active_flags.end()) {
            flag = it->second;
        }
    }

    if (flag) {
        flag->store(true, std::memory_order_relaxed);
        return true;
    }
    return false;
}

HolePunchEventEngine::ConnectionHandle HolePunchEventEngine::Connect(
    OnConnectCallback on_connect,
    const ResolvedAddress& addr,
    const grpc_exp::EndpointConfig& args,
    grpc_exp::MemoryAllocator memory_allocator,
    Duration timeout
) {

    void* arg_ptr = args.GetVoidPointer("grpc.custom.hole_punch_params");

    if (arg_ptr == nullptr) {
        return default_engine_->Connect(
            std::move(on_connect), addr, args, std::move(memory_allocator), timeout);
    }

    auto* hp_arg = static_cast<NegotiatedHolePunchArg*>(arg_ptr);

    // UPDATED: Using your clean valid() method
    if (!hp_arg->reserved_fd.valid()) {
        on_connect(absl::UnavailableError("Socket consumed by previous attempt"));
        return ConnectionHandle::kInvalid;
    }

    unique_fd reserved_fd = std::move(hp_arg->reserved_fd);
    net::HolePunchParameters params = hp_arg->params;

    intptr_t handle_id = next_connection_id_.fetch_add(1, std::memory_order_relaxed);

    // 1. Setup the atomic cancellation flag for THIS connection
    auto cancel_flag = std::make_shared<std::atomic<bool>>(false);
    {
        std::lock_guard<std::mutex> lock(tracker_->mu);
        tracker_->active_flags[handle_id] = cancel_flag;
    }

    // 2. Spawn Detached Thread
    std::thread([
        handle_id,
        fd = std::move(reserved_fd),
        params,
        poller = this->poller_,
        ssl_ctx = this->ssl_ctx_,
        on_connect = std::move(on_connect),
        tracker = this->tracker_,
        cancel_flag = cancel_flag
    ]() mutable {
        CleanupGuard cleanup_guard{tracker, handle_id};

        try {
            // Pre-check (Early Return replaces goto)
            if (cancel_flag->load(std::memory_order_relaxed)) {
                on_connect(absl::CancelledError("Hole punch cancelled before start"));
                return;
            }

            // Hole Punch
            unique_fd connected_fd = tcpv4_hole_punch(std::move(fd), params);

            // Mid-check (Early Return replaces goto)
            if (cancel_flag->load(std::memory_order_relaxed)) {
                on_connect(absl::CancelledError("Hole punch cancelled during punch"));
                return;
            }

            // Temporarily Blocking for SSL
            tcpv4_set_blocking(connected_fd, true);
            tcpv4_set_timeout(connected_fd, 5.0f); // TODO: timeout from config

            SSL_ptr ssl = SSL_new(ssl_ctx);
            SSL_set_fd(ssl, connected_fd.handle());
            SSL_set_connect_state(ssl);

            int ret = SSL_connect(ssl);
            if (ret != 1) {
                on_connect(absl::UnavailableError("TLS Handshake failed or timed out"));
                return; // unique_fd and SSL_ptr auto-cleanup, ScopeGuard erases map
            }

            // Restore non-blocking mode
            tcpv4_set_timeout(connected_fd, 0);
            tcpv4_set_blocking(connected_fd, false);

            // UPDATED: Because SSLEndpoint now takes ownership of the RAII objects
            // directly, we can use std::move!
            auto ep = std::make_unique<SSLEndpoint>(
                std::move(connected_fd),
                std::move(ssl),
                poller
            );

            on_connect(std::move(ep));
            return; // Success! ScopeGuard cleans the map.

        } catch (const std::exception& e) {
            on_connect(absl::UnavailableError(std::string("Hole punch failed: ") + e.what()));
            return; // ScopeGuard cleans the map.
        }

    }).detach();

    return {handle_id, 0};
}
