#pragma once

#include <grpcpp/grpcpp.h>


class GRPCHandler {
    public:
    grpc::ServerContext ctx;
    int state = 0;

    GRPCHandler() {}

    virtual void process(grpc::ServerCompletionQueue *cq, bool running) = 0;
    virtual ~GRPCHandler() = default;
};


template<class Tservice, class Trequest, class Tresult>
class GRPCSimpleHandler : public GRPCHandler {
    protected:
    Tservice *service = NULL;
    Trequest request;
    Tresult response;
    grpc::ServerAsyncResponseWriter<Tresult> responder;

    public:
    GRPCSimpleHandler(Tservice *service)
        : service(service), responder(&ctx) {}
    GRPCSimpleHandler(const GRPCSimpleHandler& other)
    : GRPCSimpleHandler(other.service) {}
    GRPCSimpleHandler(GRPCSimpleHandler&& other) = delete;


    void bind(grpc::ServerCompletionQueue *cq) {
        service->RequestWrite(&ctx, &request, &responder, cq, cq, this);
    }

    void handle_request() {
        throw std::runtime_error("not implemented");
    }

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
                responder.Finish(response, grpc::Status::OK, this);
                break;
            default:
                delete this;
                break;
        }
    }
};
