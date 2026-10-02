#include "ssl_endpoint.h"

#include <grpc/slice.h>


grpc_exp::EventEngine::ResolvedAddress populate_local_addr(int fd) {
    sockaddr_storage addr;
    socklen_t len = sizeof(addr);

    if (getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
        return grpc_exp::EventEngine::ResolvedAddress(
            reinterpret_cast<const sockaddr*>(&addr), len);
    }

    return grpc_exp::EventEngine::ResolvedAddress();
}

grpc_exp::EventEngine::ResolvedAddress populate_peer_addr(int fd) {
    sockaddr_storage addr;
    socklen_t len = sizeof(addr);

    if (getpeername(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
        return grpc_exp::EventEngine::ResolvedAddress(
            reinterpret_cast<const sockaddr*>(&addr), len);
    }

    return grpc_exp::EventEngine::ResolvedAddress();
}

SSLEndpoint::SSLEndpoint(unique_fd fd, SSL_ptr ssl, std::shared_ptr<PollEngine> poller)
    : fd_(std::move(fd)), ssl_(std::move(ssl)), poller_(poller) {
    SSL_set_mode(ssl_, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    peer_addr_ = populate_peer_addr(fd_);
    local_addr_ = populate_local_addr(fd_);
}

SSLEndpoint::~SSLEndpoint() {
    if (poller_) {
        poller_->pop(fd_);
    }
}

bool SSLEndpoint::Read(absl::AnyInvocable<void(absl::Status)> on_read,
                       grpc_exp::SliceBuffer* buffer, const ReadArgs /*args*/) {
    return DoRead(std::move(on_read), buffer);
}

bool SSLEndpoint::DoRead(absl::AnyInvocable<void(absl::Status)> on_read,
                         grpc_exp::SliceBuffer* buffer) {
    grpc_slice c_slice = grpc_slice_malloc(kSslReadBufferSize);

    int ret = 0;
    int err = 0;

    {
        std::lock_guard<std::mutex> lock(ssl_mutex_);
        ret = SSL_read(ssl_, GRPC_SLICE_START_PTR(c_slice), kSslReadBufferSize);
        if (ret <= 0) err = SSL_get_error(ssl_, ret);
    }

    if (ret > 0) {
        grpc_exp::Slice slice(c_slice);

        if (static_cast<size_t>(ret) < kSslReadBufferSize) {
            buffer->Append(slice.TakeSubSlice(0, ret));
        } else {
            buffer->Append(std::move(slice));
        }

        on_read(absl::OkStatus());
        return true;
    }

    grpc_slice_unref(c_slice);

    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
        bool want_read = (err == SSL_ERROR_WANT_READ);

        poller_->push(fd_,
            [this, cb = std::move(on_read), buffer]() mutable {
                DoRead(std::move(cb), buffer);
            },
            want_read,
            !want_read
        );
        return false;
    }

    on_read(absl::InternalError("SSL_read failed"));
    return true;
}

bool SSLEndpoint::Write(absl::AnyInvocable<void(absl::Status)> on_write,
                        grpc_exp::SliceBuffer* data, const WriteArgs /*args*/) {
    return DoWrite(std::move(on_write), data);
}

bool SSLEndpoint::DoWrite(absl::AnyInvocable<void(absl::Status)> on_write,
                          grpc_exp::SliceBuffer* data) {
    std::unique_lock<std::mutex> lock(ssl_mutex_);

    while (data->Count() > 0) {
        grpc_exp::Slice slice = data->TakeFirst();

        int ret = SSL_write(ssl_, slice.begin(), slice.size());

        if (ret > 0) {
            size_t written = static_cast<size_t>(ret);

            if (written < slice.size()) {
                auto remaining = slice.TakeSubSlice(written, slice.size());
                data->Prepend(std::move(remaining));

                lock.unlock();

                poller_->push(fd_, [this, cb = std::move(on_write), data]() mutable {
                    DoWrite(std::move(cb), data);
                }, false, true);

                return false;
            }

            continue;
        }

        int err = SSL_get_error(ssl_, ret);

        if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) {
            data->Prepend(std::move(slice));

            bool want_read = (err == SSL_ERROR_WANT_READ);

            lock.unlock();

            poller_->push(fd_,
                [this, cb = std::move(on_write), data]() mutable {
                    DoWrite(std::move(cb), data);
                },
                want_read,
                !want_read
            );
            return false;
        }

        lock.unlock();
        on_write(absl::InternalError("SSL_write failed"));
        return true;
    }

    lock.unlock();
    on_write(absl::OkStatus());
    return true;
}

std::shared_ptr<grpc_exp::EventEngine::Endpoint::TelemetryInfo> SSLEndpoint::GetTelemetryInfo() const {
    return nullptr;
}

const grpc_exp::EventEngine::ResolvedAddress& SSLEndpoint::GetPeerAddress() const {
    return peer_addr_;
}

const grpc_exp::EventEngine::ResolvedAddress& SSLEndpoint::GetLocalAddress() const {
    return local_addr_;
}
