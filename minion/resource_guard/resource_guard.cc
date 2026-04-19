#include <grpc/callback.h>

#include <grpcpp/grpcpp.h>
#include <grpcpp/server_builder.h>

#include <map>
#include <memory>
#include <string>

#include <resource.grpc.pb.h>
#include <resource.pb.h>

enum class PendingOp { kRead, kWrite, kFinish };

enum class ProcessResult { AckWrite, StreamFinished };

class LimitHandler : public GRPCStreamHandler<
                         LimitHandler,
                         resource::Resource::AsyncService,
                         resource::ResourceRequestUnion,
                         resource::ResourceRequestResult> {
    PendingOp pending_ = PendingOp::kRead;

    std::map<std::string, double> res_request_;

public:
    explicit LimitHandler(resource::Resource::AsyncService* async_service)
        : GRPCStreamHandler(async_service) {}

    LimitHandler(const LimitHandler& other)
        : GRPCStreamHandler(other.service) {}

    void bind(grpc::ServerCompletionQueue* cq) override {
        service->RequestResourceSession(&ctx, &stream, cq, cq, this);
    }

    void on_stream_rpc_connected(grpc::ServerCompletionQueue* cq) override {
        (void)cq;
        pending_ = PendingOp::kRead;
        read();
    }

    void process(grpc::ServerCompletionQueue* cq, bool running) override {
        if (stream_connect_phase_ != StreamConnectPhase::kStreaming) {
            handle_stream_connect(cq, running);
            return;
        }

        switch (pending_) {
            case PendingOp::kRead:
                handle_read_completion(running);
                break;
            case PendingOp::kWrite:
                handle_write_completion(running);
                break;
            case PendingOp::kFinish:
                complete_after_stream_finish();
                break;
        }
    }

private:
    ProcessResult process_incoming_message() {
        switch (request.v_case()) {
            case resource::ResourceRequestUnion::kResourceRequest:
                // TODO: validate hdr, init billing / auth paths
                break;
            case resource::ResourceRequestUnion::kBillAppend:
                for (const auto& v : request.billappend().billitems()) {
                    res_request_[v.first] += v.second;
                }
                break;
            case resource::ResourceRequestUnion::kBillOverwrite:
                res_request_.clear();
                for (const auto& v : request.billoverwrite().billitems()) {
                    res_request_[v.first] = v.second;
                }
                break;
            case resource::ResourceRequestUnion::kResourceFinish:
                switch (request.resourcefinish().result()) {
                    case resource::OperationResult::OK:
                        break;
                    case resource::OperationResult::FAILED:
                        break;
                    case resource::OperationResult::CANCELLED:
                        break;
                    case resource::OperationResult::TIMEOUT:
                        break;
                    case resource::OperationResult::OVERLOAD:
                        break;
                    default:
                        break;
                }
                request.clear_v();
                pending_ = PendingOp::kFinish;
                finish(grpc::Status::OK);
                return ProcessResult::StreamFinished;
            case resource::ResourceRequestUnion::V_NOT_SET:
                break;
        }

        response.set_result(resource::OperationResult::OK);
        return ProcessResult::AckWrite;
    }

    void handle_read_completion(bool ok) {
        if (!ok) {
            pending_ = PendingOp::kFinish;
            finish(grpc::Status::OK);
            return;
        }

        const ProcessResult pr = process_incoming_message();
        if (pr == ProcessResult::StreamFinished) {
            return;
        }
        if (pr == ProcessResult::AckWrite) {
            request.clear_v();
            pending_ = PendingOp::kWrite;
            write();
            return;
        }
        request.clear_v();
        pending_ = PendingOp::kRead;
        read();
    }

    void handle_write_completion(bool ok) {
        if (!ok) {
            pending_ = PendingOp::kFinish;
            finish(grpc::Status::OK);
            return;
        }
        pending_ = PendingOp::kRead;
        read();
    }

    void complete_after_stream_finish() {
        grpc_defer_handler_destroy(std::unique_ptr<GRPCHandler>(self_.release()));
    }
};

int main() {
    const char* listen = "0.0.0.0:50053";

    resource::Resource::AsyncService async_service;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(listen, grpc::InsecureServerCredentials());
    builder.RegisterService(&async_service);
    std::unique_ptr<grpc::ServerCompletionQueue> cq = builder.AddCompletionQueue();
    std::unique_ptr<grpc::Server> server = builder.BuildAndStart();

    grpc_prime_async_handler(std::make_unique<LimitHandler>(&async_service), cq.get(), true);

    void* tag = nullptr;
    bool ok = true;
    while (cq->Next(&tag, &ok)) {
        static_cast<GRPCHandler*>(tag)->process(cq.get(), ok);
        grpc_run_deferred_handler_destroys();
    }

    return 0;
}
