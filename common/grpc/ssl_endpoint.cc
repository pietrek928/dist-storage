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

void SSLEndpoint::Read(absl::AnyInvocable<void(absl::Status)> on_read,
            grpc_exp::SliceBuffer* buffer, const ReadArgs* args) {
    std::lock_guard<std::mutex> lock(ssl_mutex);

    char temp_buf[ssl_read_buffer_size];
    int ret = SSL_read(ssl, temp_buf, sizeof(temp_buf));

    if (ret > 0) {
        buffer->Append(grpc_exp::Slice::FromCopiedBuffer(temp_buf, ret));
        on_read(absl::OkStatus());
        return;
    }

    int err = SSL_get_error(ssl, ret);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
        bool want_read = (err == SSL_ERROR_WANT_READ);
        poller->push(fd, [this, cb = std::move(on_read), buffer]() mutable {
            this->Read(std::move(cb), buffer, nullptr);
        }, want_read, !want_read);
        return;
    }

    on_read(absl::InternalError("SSL_read failed"));
}

void SSLEndpoint::Write(absl::AnyInvocable<void(absl::Status)> on_write,
        grpc_exp::SliceBuffer* data, const WriteArgs* args) {
    std::lock_guard<std::mutex> lock(ssl_mutex);

    if (data->Count() == 0) {
        // on_read_or_write_finished(std::move(on_write), absl::OkStatus());
        on_write(absl::OkStatus());
        return;
    }

    // Take the first slice to attempt writing
    auto first = data->TakeFirst();

    // SSL_write needs a raw pointer and size
    int ret = SSL_write(ssl, first.begin(), first.size());

    if (ret > 0) {
        size_t written = static_cast<size_t>(ret);
        if (written < first.size()) {
            // PARTIAL WRITE: Create a new slice for the remaining part
            // We use the pointer-based constructor for the remaining segment
            auto remaining = grpc_exp::Slice::FromCopiedBuffer(
                reinterpret_cast<const char*>(first.begin() + written),
                first.size() - written
            );
            data->Prepend(std::move(remaining));

            // Re-register for when the socket is writable again
            poller->push(fd, [this, cb = std::move(on_write), data]() mutable {
                this->Write(std::move(cb), data, nullptr);
            }, false, true);
            return;
        }

        // FULL SLICE WRITTEN: Check if more slices remain
        if (data->Count() == 0) {
            // on_read_or_write_finished(std::move(on_write), absl::OkStatus());
            on_write(absl::OkStatus());
        } else {
            this->Write(std::move(on_write), data, args);
        }
        return;
    }

    int err = SSL_get_error(ssl, ret);
    if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) {
        bool want_write = (err == SSL_ERROR_WANT_WRITE);

        // Put the original slice back entirely
        data->Prepend(std::move(first));

        poller->push(fd, [this, cb = std::move(on_write), data]() mutable {
            this->Write(std::move(cb), data, nullptr);
        }, !want_write, want_write);
        return;
    }

    on_write(absl::InternalError("SSL_write failed"));
}

// Required boilerplate for EventEngine
const grpc_exp::EventEngine::ResolvedAddress& SSLEndpoint::GetPeerAddress() const { return peer_addr; }
const grpc_exp::EventEngine::ResolvedAddress& SSLEndpoint::GetLocalAddress() const { return local_addr; }
