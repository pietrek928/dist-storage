#pragma once

#include <mutex>
#include <grpc/event_engine/event_engine.h>

#include <utils/unique_fd.h>
#include <utils/guard_ptr.h>
#include <utils/poll_engine.h>
#include <crypto/ssl.h>


namespace grpc_exp = grpc_event_engine::experimental;

grpc_exp::EventEngine::ResolvedAddress populate_local_addr(int fd);
grpc_exp::EventEngine::ResolvedAddress populate_peer_addr(int fd);


class SSLEndpoint : public grpc_exp::EventEngine::Endpoint {
    // TODO: is it needed ?
    // void on_read_or_write_finished(absl::AnyInvocable<void(absl::Status)> cb, absl::Status status) {
    //     // Dispatching to gRPC's default engine is the safest way to trigger the next step
    //     grpc_exp::GetDefaultEventEngine()->Run(
    //         [cb = std::move(cb), status]() mutable {
    //             cb(status);
    //         }
    //     );
    // }
    constexpr static int ssl_read_buffer_size = 8192;

    unique_fd fd;
    SSL_ptr ssl;
    PollEngine* poller;
    std::mutex ssl_mutex;
    grpc_exp::EventEngine::ResolvedAddress peer_addr;
    grpc_exp::EventEngine::ResolvedAddress local_addr;

public:
    SSLEndpoint(int fd, SSL* ssl, PollEngine* poller);
    ~SSLEndpoint();

    // Read and Write calling SSL_read and SSL_write
    bool Read(absl::AnyInvocable<void(absl::Status)> on_read,
              grpc_exp::SliceBuffer* buffer, const ReadArgs args) override;
    bool Write(absl::AnyInvocable<void(absl::Status)> on_write,
           grpc_exp::SliceBuffer* data, const WriteArgs args) override;

    // Required boilerplate for EventEngine
    const grpc_exp::EventEngine::ResolvedAddress& GetPeerAddress() const override;
    const grpc_exp::EventEngine::ResolvedAddress& GetLocalAddress() const override;
};
