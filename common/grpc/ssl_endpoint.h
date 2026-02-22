#pragma once

#include <mutex>
#include <memory>
#include <grpc/event_engine/event_engine.h>
#include <grpc/event_engine/slice.h>
#include <grpc/event_engine/slice_buffer.h>

#include <utils/unique_fd.h>
#include <utils/guard_ptr.h>
#include <utils/poll_engine.h>
#include <crypto/ssl.h>

namespace grpc_exp = grpc_event_engine::experimental;

class SSLEndpoint : public grpc_exp::EventEngine::Endpoint,
                    public std::enable_shared_from_this<SSLEndpoint> {

    // 16KB is a standard TLS record size
    constexpr static int kSslReadBufferSize = 16 * 1024;

    unique_fd fd_;
    SSL_ptr ssl_;
    std::shared_ptr<PollEngine> poller_;
    std::mutex ssl_mutex_;

    grpc_exp::EventEngine::ResolvedAddress peer_addr_;
    grpc_exp::EventEngine::ResolvedAddress local_addr_;

    // Internal helpers to handle logic without mutex contention
    void DoRead(absl::AnyInvocable<void(absl::Status)> on_read,
                grpc_exp::SliceBuffer* buffer);
    void DoWrite(absl::AnyInvocable<void(absl::Status)> on_write,
                 grpc_exp::SliceBuffer* data);

public:
    SSLEndpoint(int fd, SSL* ssl, std::shared_ptr<PollEngine> poller);
    ~SSLEndpoint() override;

    bool Read(absl::AnyInvocable<void(absl::Status)> on_read,
              grpc_exp::SliceBuffer* buffer, const ReadArgs args) override;

    bool Write(absl::AnyInvocable<void(absl::Status)> on_write,
               grpc_exp::SliceBuffer* data, const WriteArgs args) override;

    const grpc_exp::EventEngine::ResolvedAddress& GetPeerAddress() const override;
    const grpc_exp::EventEngine::ResolvedAddress& GetLocalAddress() const override;
};