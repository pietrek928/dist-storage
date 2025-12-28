#include "ssl_endpoint.h"


grpc_exp::EventEngine::ResolvedAddress populate_local_addr(int fd) {
    sockaddr_storage addr;
    socklen_t len = sizeof(addr);

    // Populate Local Address
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

SSLEndpoint::SSLEndpoint(int fd, SSL* ssl, PollEngine* poller)
    : fd(fd), ssl(ssl), poller(poller) {
    // Ensure SSL is in non-blocking mode
    SSL_set_mode(ssl, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    peer_addr = populate_peer_addr(fd);
    local_addr = populate_local_addr(fd);
}

SSLEndpoint::~SSLEndpoint() {
    poller->pop(fd);
}

bool SSLEndpoint::Read(absl::AnyInvocable<void(absl::Status)> on_read,
            grpc_exp::SliceBuffer* buffer, const ReadArgs args) {
    std::lock_guard<std::mutex> lock(ssl_mutex);

    char temp_buf[ssl_read_buffer_size];
    int ret = SSL_read(ssl, temp_buf, sizeof(temp_buf));

    if (ret > 0) {
        buffer->Append(grpc_exp::Slice::FromCopiedBuffer(temp_buf, ret));
        on_read(absl::OkStatus());
        return true;
    }

    int err = SSL_get_error(ssl, ret);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
        bool want_read = (err == SSL_ERROR_WANT_READ);
        poller->push(fd, [this, cb = std::move(on_read), buffer]() mutable {
            this->Read(std::move(cb), buffer, ReadArgs{});
        }, want_read, !want_read);
        return false;
    }

    on_read(absl::InternalError("SSL_read failed"));
    return true;
}

bool SSLEndpoint::Write(absl::AnyInvocable<void(absl::Status)> on_write,
        grpc_exp::SliceBuffer* data, const WriteArgs args) {
    std::lock_guard<std::mutex> lock(ssl_mutex);

    // Try to write as much as possible in a loop to avoid stack overflow
    while (data->Count() > 0) {
        auto first = data->TakeFirst();
        int ret = SSL_write(ssl, first.begin(), first.size());

        if (ret > 0) {
            size_t written = static_cast<size_t>(ret);
            if (written < first.size()) {
                // Partial write - put back the remainder
                auto remaining = grpc_exp::Slice::FromCopiedBuffer(
                    reinterpret_cast<const char*>(first.begin() + written),
                    first.size() - written
                );
                data->Prepend(std::move(remaining));

                poller->push(fd, [this, cb = std::move(on_write), data]() mutable {
                    this->Write(std::move(cb), data, WriteArgs{});
                }, false, true);
                return false;
            }
            // Slice finished, continue loop to next slice
        } else {
            int err = SSL_get_error(ssl, ret);
            if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) {
                data->Prepend(std::move(first));
                poller->push(fd, [this, cb = std::move(on_write), data]() mutable {
                    this->Write(std::move(cb), data, WriteArgs{});
                }, (err == SSL_ERROR_WANT_READ), (err == SSL_ERROR_WANT_WRITE));
                return false;
            }
            on_write(absl::InternalError("SSL_write failed"));
            return true;
        }
    }

    on_write(absl::OkStatus());
    return true;
}

// Required boilerplate for EventEngine
const grpc_exp::EventEngine::ResolvedAddress& SSLEndpoint::GetPeerAddress() const { return peer_addr; }
const grpc_exp::EventEngine::ResolvedAddress& SSLEndpoint::GetLocalAddress() const { return local_addr; }
