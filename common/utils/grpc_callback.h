#pragma once

#include <grpcpp/grpcpp.h>
#include <grpcpp/support/async_stream.h>


class GRPCHandler {
    public:
    GRPCHandler() {}

    // CAUTION: this function MUST emit event or call dispose() at the end
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

class GRPCAllocableHandler : public GRPCHandler {
    protected:
    grpc::ServerContext ctx;
    int state = 0;
    void dispose() {
        delete this;
    }
};

template<class Tservice, class Trequest, class Tresult>
class GRPCBasicHandler : public GRPCAllocableHandler {
    protected:
    Tservice *service = NULL;
    Trequest request;
    Tresult response;
    grpc::ServerAsyncResponseWriter<Tresult> responder;

    void bind(grpc::ServerCompletionQueue *cq) {
        service->RequestWrite(&ctx, &request, &responder, cq, cq, this);
    }

    void handle_request() {
        throw std::runtime_error("not implemented");
    }

    void finish() {
        responder.Finish(response, grpc::Status::OK, this);
        state = 12345;
    }

    public:
    GRPCBasicHandler(Tservice *service)
        : service(service), responder(&ctx) {}
    GRPCBasicHandler(const GRPCBasicHandler& other)
    : GRPCBasicHandler(other.service) {}
    GRPCBasicHandler(GRPCBasicHandler&& other) = delete;

    void process(grpc::ServerCompletionQueue *cq, bool running) {
        switch(state++) {
            case 0: bind(cq); break;
            case 1:
                if (running) {  // Clone for new request
                    (new decltype(*this)(*this))->process(cq, running);
                }
                break;
            case 2:
                handle_request();
                finish();
                break;
            default:
                dispose();
                break;
        }
    }
};


template<class Tservice, class Trequest, class Tresult>
class GRPCStreamHandler : public GRPCAllocableHandler {
    protected:
    Trequest request;
    Tresult response;
    grpc::ServerAsyncReaderWriter<Tresult, Trequest> stream;

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
    GRPCStreamHandler(const GRPCStreamHandler& other)
    : GRPCStreamHandler() {}
    GRPCStreamHandler(GRPCStreamHandler&& other) = delete;
};
