#pragma once

#include "callback.h"


void grpc_loop(grpc::ServerCompletionQueue *cq) {
    void* tag;
    bool ok;  // TODO: stop condition
    while (cq->Next(&tag, &ok)) {
        if (!ok) {
            // std::cout << "RPC failed" << std::endl;
            continue;
        }

        static_cast<GRPCHandler*>(tag)->process(cq, true);
    }
}
