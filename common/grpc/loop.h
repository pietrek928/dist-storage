#pragma once

#include <grpcpp/grpcpp.h>

void grpc_loop(grpc::ServerCompletionQueue *cq);
