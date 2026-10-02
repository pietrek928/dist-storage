#include "loop.h"

#include "callback.h"


void grpc_loop(grpc::ServerCompletionQueue *cq) {
    void* tag;
    bool ok = true;
    while (cq->Next(&tag, &ok)) {
        static_cast<GRPCHandler*>(tag)->process(cq, ok);
        grpc_run_deferred_handler_destroys();
    }
}
