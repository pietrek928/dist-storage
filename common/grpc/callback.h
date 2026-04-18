#pragma once

#include <grpcpp/grpcpp.h>
#include <grpcpp/support/async_stream.h>

#include <atomic>
#include <memory>


class GRPCHandler {
    public:
    GRPCHandler() {}

    // CAUTION: this function MUST emit another CQ event or destroy the handler safely at the end.
    virtual void process(grpc::ServerCompletionQueue *cq, bool running) = 0;
    virtual ~GRPCHandler() = default;
};

class GRPCCounterHandler : public GRPCHandler {
    std::atomic<int> counter = 0;
    GRPCHandler *handler;

    public:
    GRPCCounterHandler() = delete;
    GRPCCounterHandler(GRPCHandler *handler)
        : handler(handler) {}

    virtual void process(grpc::ServerCompletionQueue *cq, bool running) {
        counter++;
        handler->process(cq, running);
    }

    void reset() {
        counter = 0;
    }

    int get() {
        return counter;
    }
};

/// Clone the next async acceptor: heap-allocate `T` from `proto`, self-own via `arm`, then prime CQ.
template<typename T>
void grpc_async_clone_acceptor(const T& proto, grpc::ServerCompletionQueue* cq, bool running) {
    if (running) {
        auto p = std::make_unique<T>(proto);
        T* raw = p.get();
        raw->arm(std::move(p));
        raw->process(cq, running);
    }
}

/// Prime the first async handler: transfer ownership into `arm` then start the CQ state machine.
template<class T>
void grpc_prime_async_handler(std::unique_ptr<T> p, grpc::ServerCompletionQueue* cq, bool running) {
    T* raw = p.get();
    raw->arm(std::move(p));
    raw->process(cq, running);
}

template<class Tderived, class Tservice, class Trequest, class Tresult>
class GRPCBasicHandler : public GRPCHandler {
    protected:
    grpc::ServerContext ctx;
    int state = 0;

    Tservice *service = NULL;
    Trequest request;
    Tresult response;
    grpc::ServerAsyncResponseWriter<Tresult> responder;

    /// Self-ownership for the handler lifetime (CQ tags use `this` until `Finish` completes).
    std::unique_ptr<Tderived> self_;

    virtual void bind(grpc::ServerCompletionQueue *cq) = 0;

    virtual void handle_request() {
        throw std::runtime_error("not implemented");
    }

    virtual grpc::Status finish_grpc_status() {
        return grpc::Status::OK;
    }

    void finish() {
        responder.Finish(response, finish_grpc_status(), this);
        state = 12345;  // last state value
    }

    public:
    GRPCBasicHandler(Tservice *service)
        : service(service), responder(&ctx) {}
    GRPCBasicHandler(const GRPCBasicHandler& other)
        : service(other.service), responder(&ctx) {}
    GRPCBasicHandler(GRPCBasicHandler&& other) = delete;

    void arm(std::unique_ptr<Tderived> self) { self_ = std::move(self); }

    void process(grpc::ServerCompletionQueue *cq, bool running) {
        switch(state++) {
            case 0: this->bind(cq); break;
            case 1:
                grpc_async_clone_acceptor(static_cast<const Tderived&>(*this), cq, running);
                break;
            case 2:
                handle_request();
                finish();
                break;
            default:
                self_.reset();
                return;
        }
    }
};


template<class Tderived, class Tservice, class Trequest, class Tresult>
class GRPCStreamHandler : public GRPCHandler {
    protected:
    grpc::ServerContext ctx;

    enum class StreamConnectPhase { kInitial, kAccepted, kStreaming };

    StreamConnectPhase stream_connect_phase_ = StreamConnectPhase::kInitial;

    // Used by RequestStream / Request*. Clone preserves this pointer (ctx/stream are fresh).
    Tservice* service = nullptr;
    Trequest request;
    Tresult response;
    grpc::ServerAsyncReaderWriter<Tresult, Trequest> stream;

    /// Optional self-ownership when not using `shared_ptr` (see `MessageStreamHandler`).
    std::unique_ptr<Tderived> self_;

    virtual void bind(grpc::ServerCompletionQueue* cq) { (void)cq; }

    /// After `RequestStream` completes: clone next acceptor, then start this stream (e.g. `read()`).
    virtual void on_stream_rpc_connected(grpc::ServerCompletionQueue* cq) { (void)cq; }

    /// RequestStream completion with ok=false (default: release self-ownership).
    virtual void on_stream_connect_failed() { self_.reset(); }

    /// Clone handler for the next incoming stream RPC (default: heap clone + process).
    virtual void clone_acceptor_for_next_request(grpc::ServerCompletionQueue* cq, bool running) {
        grpc_async_clone_acceptor(static_cast<const Tderived&>(*this), cq, running);
    }

    /// First completion: `bind`; second: clone + `on_stream_rpc_connected` (same completion as unary
    /// clone + next step, but stream starts `read()` here).
    void handle_stream_connect(grpc::ServerCompletionQueue* cq, bool running) {
        if (stream_connect_phase_ == StreamConnectPhase::kInitial) {
            stream_connect_phase_ = StreamConnectPhase::kAccepted;
            bind(cq);
            return;
        }
        if (stream_connect_phase_ == StreamConnectPhase::kAccepted) {
            if (!running) {
                on_stream_connect_failed();
                return;
            }
            stream_connect_phase_ = StreamConnectPhase::kStreaming;
            clone_acceptor_for_next_request(cq, running);
            on_stream_rpc_connected(cq);
        }
    }

    void read() {
        stream.Read(&request, this);
    }

    void read(GRPCHandler *handler) {
        stream.Read(&request, handler);
    }

    void write() {
        stream.Write(response, this);
    }

    void write(GRPCHandler *handler) {
        stream.Write(response, handler);
    }

    void finish(grpc::Status status = grpc::Status::OK) {
        stream.Finish(status, this);
    }

    public:
    GRPCStreamHandler()
        : stream(&ctx) {}
    explicit GRPCStreamHandler(Tservice* svc)
        : service(svc), stream(&ctx) {}
    GRPCStreamHandler(const GRPCStreamHandler& other)
        : service(other.service), stream(&ctx) {}
    GRPCStreamHandler(GRPCStreamHandler&& other) = delete;

    void arm(std::unique_ptr<Tderived> self) { self_ = std::move(self); }
};
