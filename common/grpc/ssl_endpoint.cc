#include "ssl_endpoint.h"

#include <grpc/slice.h>       // For C-core slice allocators if available
#include <grpc/support/log.h>


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

SSLEndpoint::SSLEndpoint(int fd, SSL* ssl, std::shared_ptr<PollEngine> poller)
    : fd_(fd), ssl_(ssl), poller_(poller) {
    // Enable partial writes to handle non-blocking IO correctly
    SSL_set_mode(ssl_, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    peer_addr_ = populate_peer_addr(fd_);
    local_addr_ = populate_local_addr(fd_);
}

SSLEndpoint::~SSLEndpoint() {
    // Ensure we unregister from poller so no callbacks fire after destruction
    if (poller_) {
        poller_->pop(fd_);
    }
}

bool SSLEndpoint::Read(absl::AnyInvocable<void(absl::Status)> on_read,
                       grpc_exp::SliceBuffer* buffer, const ReadArgs /*args*/) {
    // We delegate to a helper to allow easy recursion/looping without duplicating logic
    DoRead(std::move(on_read), buffer);
    // Return false effectively says "Async or handled"; standard EventEngine pattern
    // usually implies returning `true` if Sync completion occurred, but for safety
    // in custom pollers, always returning false and invoking callback is easier.
    // However, if your contract requires returning true on Sync:
    return false;
}

void SSLEndpoint::DoRead(absl::AnyInvocable<void(absl::Status)> on_read,
                         grpc_exp::SliceBuffer* buffer) {

    // 1. Allocate a raw C-slice. This gives us a mutable buffer.
    grpc_slice c_slice = grpc_slice_malloc(kSslReadBufferSize);

    int ret = 0;
    int err = 0;

    {
        std::lock_guard<std::mutex> lock(ssl_mutex_);
        // 2. Write directly into the raw slice pointer
        ret = SSL_read(ssl_, GRPC_SLICE_START_PTR(c_slice), kSslReadBufferSize);
        if (ret <= 0) err = SSL_get_error(ssl_, ret);
    }

    if (ret > 0) {
        // 3. Transfer ownership to C++ Wrapper
        grpc_exp::Slice slice(c_slice);

        // 4. If we read less than the buffer size, trim the slice
        if (static_cast<size_t>(ret) < kSslReadBufferSize) {
            // TakeSubSlice returns a new slice sharing the ref, logic inside handles lightweight views
            buffer->Append(slice.TakeSubSlice(0, ret));
        } else {
            buffer->Append(std::move(slice));
        }

        on_read(absl::OkStatus());
        return;
    }

    // --- Cleanup on Failure/Retry ---
    // Since we didn't wrap 'c_slice' in a C++ object in the failure path,
    // we must manually unref it to avoid a memory leak.
    grpc_slice_unref(c_slice);

    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
        bool want_read = (err == SSL_ERROR_WANT_READ);
        auto self = shared_from_this();

        poller_->push(fd_,
            [self, cb = std::move(on_read), buffer, want_read]() mutable {
                self->DoRead(std::move(cb), buffer);
            },
            want_read,
            !want_read
        );
        return;
    }

    on_read(absl::InternalError("SSL_read failed"));
}

bool SSLEndpoint::Write(absl::AnyInvocable<void(absl::Status)> on_write,
                        grpc_exp::SliceBuffer* data, const WriteArgs /*args*/) {
    DoWrite(std::move(on_write), data);
    return false;
}

void SSLEndpoint::DoWrite(absl::AnyInvocable<void(absl::Status)> on_write,
                          grpc_exp::SliceBuffer* data) {
    std::unique_lock<std::mutex> lock(ssl_mutex_);

    while (data->Count() > 0) {
        // Take the first slice.
        // RAII: This 'slice' object will be destroyed properly when we return or loop.
        grpc_exp::Slice slice = data->TakeFirst();

        int ret = SSL_write(ssl_, slice.begin(), slice.size());

        // --- Case 1: Success (Full or Partial) ---
        if (ret > 0) {
            size_t written = static_cast<size_t>(ret);

            // 1a. Partial Write
            if (written < slice.size()) {
                // Put back the unwritten part
                auto remaining = slice.TakeSubSlice(written, slice.size());
                data->Prepend(std::move(remaining));

                // Unlock immediately before scheduling
                lock.unlock();

                // Schedule wait for socket to be writable
                auto self = shared_from_this();
                poller_->push(fd_, [self, cb = std::move(on_write), data]() mutable {
                    self->DoWrite(std::move(cb), data);
                }, false, true); // false=Read, true=Write

                return; // cleanly exit
            }

            // 1b. Full Write:
            // 'slice' goes out of scope here, destructor runs, loop continues.
            continue;
        }

        // --- Case 2: Error or WANT_READ/WRITE ---
        int err = SSL_get_error(ssl_, ret);

        if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) {
            // Put the entire slice back so we retry it next time
            data->Prepend(std::move(slice));

            bool want_read = (err == SSL_ERROR_WANT_READ);

            lock.unlock();

            auto self = shared_from_this();
            poller_->push(fd_,
                [self, cb = std::move(on_write), data]() mutable {
                    self->DoWrite(std::move(cb), data);
                },
                want_read,
                !want_read
            );
            return; // cleanly exit
        }

        // --- Case 3: Fatal Error ---
        lock.unlock();
        on_write(absl::InternalError("SSL_write failed"));
        return;
    }

    // --- Case 4: Buffer Empty (Success) ---
    lock.unlock();
    on_write(absl::OkStatus());
}

const grpc_exp::EventEngine::ResolvedAddress& SSLEndpoint::GetPeerAddress() const {
    return peer_addr_;
}

const grpc_exp::EventEngine::ResolvedAddress& SSLEndpoint::GetLocalAddress() const {
    return local_addr_;
}
