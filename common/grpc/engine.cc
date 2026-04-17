#include "engine.h"

#include <thread>
#include <grpc/event_engine/endpoint_config.h>

#include <absl/log/log.h>

#include <net/tcpv4.h>

#include "resolver.h"
#include "ssl_endpoint.h"


static void PerformHolePunchAndHandshake(
    unique_fd fd,
    net::HolePunchParameters params,
    bool is_client, // true = SSL_connect, false = SSL_accept
    std::shared_ptr<PollEngine> poller,
    SSL_CTX* ssl_ctx,
    std::shared_ptr<std::atomic<bool>> cancel_flag, // Nullable for server
    absl::AnyInvocable<void(absl::StatusOr<std::unique_ptr<SSLEndpoint>>)> on_complete
) {
    try {
        if (cancel_flag && cancel_flag->load(std::memory_order_relaxed)) {
            on_complete(absl::CancelledError("Cancelled before punch"));
            return;
        }

        unique_fd connected_fd = tcpv4_hole_punch(std::move(fd), params);

        if (cancel_flag && cancel_flag->load(std::memory_order_relaxed)) {
            on_complete(absl::CancelledError("Cancelled during punch"));
            return;
        }

        tcpv4_set_blocking(connected_fd, true);
        tcpv4_set_timeout(connected_fd, 5.0f);

        SSL_ptr ssl = SSL_new(ssl_ctx);
        SSL_set_fd(ssl, connected_fd.handle());

        // --- THE MAGIC TOGGLE ---
        int ret;
        if (is_client) {
            SSL_set_connect_state(ssl);
            ret = SSL_connect(ssl);
        } else {
            SSL_set_accept_state(ssl);
            ret = SSL_accept(ssl);
        }

        if (ret != 1) {
            on_complete(absl::UnavailableError("TLS Handshake failed or timed out"));
            return;
        }

        tcpv4_set_timeout(connected_fd, 0);
        tcpv4_set_blocking(connected_fd, false);

        auto ep = std::make_unique<SSLEndpoint>(
            std::move(connected_fd), std::move(ssl), poller);

        on_complete(std::move(ep));

    } catch (const std::exception& e) {
        on_complete(absl::UnavailableError(std::string("Hole punch failed: ") + e.what()));
    }
}

HolePunchEventEngine::HolePunchEventEngine(std::shared_ptr<PollEngine> poller, SSL_CTX* ssl_ctx)
    : default_engine_(grpc_exp::CreateEventEngine()),
        poller_(std::move(poller)),
        ssl_ctx_(ssl_ctx) {}

void HolePunchEventEngine::Run(Closure* closure) {
    default_engine_->Run(closure);
}

HolePunchEventEngine::TaskHandle HolePunchEventEngine::RunAfter(
    Duration when, Closure* closure
) {
    return default_engine_->RunAfter(when, closure);
}

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

    auto cancel_flag = std::make_shared<std::atomic<bool>>(false);
    {
        std::lock_guard<std::mutex> lock(tracker_->mu);
        tracker_->active_flags[handle_id] = cancel_flag;
    }

    // Spawn Detached Thread
    std::thread([
        handle_id,
        fd = std::move(reserved_fd),
        params,
        poller = this->poller_,
        ssl_ctx = this->ssl_ctx_,
        on_connect = std::move(on_connect),
        tracker = this->tracker_,
        cancel_flag = std::move(cancel_flag)
    ]() mutable {

        // RAII Guard keeps the map clean
        CleanupGuard cleanup_guard{tracker, handle_id};

        // Call the shared static worker
        PerformHolePunchAndHandshake(
            std::move(fd), params,
            true, // is_client = TRUE
            poller, ssl_ctx, cancel_flag,

            // Wrap gRPC's on_connect callback
            [cb = std::move(on_connect)](absl::StatusOr<std::unique_ptr<SSLEndpoint>> result) mutable {
                if (result.ok()) {
                    cb(std::move(*result));
                } else {
                    cb(result.status());
                }
            }
        );

    }).detach();

    return {handle_id, 0};
}

// 1. Update CreateListener to actually build and store our custom listener
absl::StatusOr<std::unique_ptr<grpc_exp::EventEngine::Listener>> HolePunchEventEngine::CreateListener(
        grpc_exp::EventEngine::Listener::AcceptCallback on_accept,
        absl::AnyInvocable<void(absl::Status)> on_shutdown,
        const grpc_exp::EndpointConfig& config,
        std::unique_ptr<grpc_exp::MemoryAllocatorFactory> memory_allocator_factory
) {

        // 1. Create the Listener and pass the cleanup lambda
        auto listener = std::make_unique<HolePunchListener>(
            std::move(on_accept),
            std::move(memory_allocator_factory),
            [this]() {
                // This fires when gRPC shuts down the Server!
                std::lock_guard<std::mutex> lock(this->listener_mu_);
                this->active_listener_ = nullptr;
            }
        );

        // 2. Store the raw observer pointer safely
        {
            std::lock_guard<std::mutex> lock(listener_mu_);
            active_listener_ = listener.get();
        }

        // 3. Hand exclusive ownership to gRPC
        return listener;
    }

// 2. Add a public method your RPC handlers can call!
void HolePunchEventEngine::AcceptIncomingHolePunch(std::unique_ptr<grpc_exp::EventEngine::Endpoint> ep) {
    HolePunchListener* listener = nullptr;
    {
        std::lock_guard<std::mutex> lock(listener_mu_);
        listener = active_listener_;
    }

    if (listener) {
        listener->InjectHolePunchedConnection(std::move(ep));
    } else {
        LOG(ERROR) << "Cannot inject connection: gRPC Server is not listening!";
    }
}

absl::Status HolePunchEventEngine::RunSignaledIncomingHolePunch(
    unique_fd reserved_fd, net::HolePunchParameters params
) {
    absl::StatusOr<std::unique_ptr<grpc_exp::EventEngine::Endpoint>> outcome =
        absl::FailedPreconditionError("hole punch not completed");

    PerformHolePunchAndHandshake(
        std::move(reserved_fd), params,
        false,
        poller_, ssl_ctx_, nullptr,
        [&](absl::StatusOr<std::unique_ptr<SSLEndpoint>> r) {
            if (!r.ok()) {
                outcome = r.status();
                return;
            }
            std::unique_ptr<SSLEndpoint> up = std::move(r).value();
            std::unique_ptr<grpc_exp::EventEngine::Endpoint> ep(
                static_cast<grpc_exp::EventEngine::Endpoint*>(up.release()));
            outcome = std::move(ep);
        }
    );

    if (!outcome.ok()) {
        return outcome.status();
    }
    AcceptIncomingHolePunch(std::move(outcome).value());
    return absl::OkStatus();
}
