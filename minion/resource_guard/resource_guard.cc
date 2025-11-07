#include <grpc_callback.h>

#include <resource.pb.h>
#include <resource.grpc.pb.h>
#include <grpcpp/grpcpp.h>


class LimitHandler : public GRPCStreamHandler<
    resource::Resource::AsyncService, resource::ResourceRequestUnion, resource::ResourceResponseUnion
> {
    GRPCCounterHandler read_count;

    std::map<std::string, double> res_request;
    std::map<std::string, double> res_available;

    bool process_request() {
        switch (request.v_case()) {
            case resource::ResourceRequestUnion::kResourceRequest:
                break;
            case resource::ResourceRequestUnion::kBillAppend:
                for (auto &v: request.billappend().billitems()) {
                    res_request[v.first] += v.second;
                }
                break;
            case resource::ResourceRequestUnion::kBillOverwrite:
                res_request.clear();
                for (auto &v: request.billappend().billitems()) {
                    res_request[v.first] = v.second;
                }
                break;
            case resource::ResourceRequestUnion::kResourceFinish: // TODO: send request to storage
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
                finish();
                return true;
            case resource::ResourceRequestUnion::V_NOT_SET:
                break;
        }
        return false;
    }

    public:
    LimitHandler() : read_count(this) {}
    LimitHandler(const LimitHandler& other) : LimitHandler() {}

    void process(grpc::ServerCompletionQueue *cq, bool running) {
        if (request.v_case() != resource::ResourceRequestUnion::V_NOT_SET) {
            bool event_emitted = process_request();
            request.clear_v();
            if (event_emitted) {
                return;
            }
        }

        switch(state++) {
            case 0: read(); break;
            case 1:
                if (running) {  // Clone for new request
                    (new LimitHandler(*this))->process(cq, running);
                }
                break;
            case 2: read(); state=2; break;
            case 3:
                finish();
                break;
            default:
                dispose();
                break;
        }
    }
};
