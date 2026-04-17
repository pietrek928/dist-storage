#include <utils/unique_fd.h>
#include <utils/task_runner.h>
#include <grpc/callback.h>

#include <fcntl.h>
#include <unistd.h>

#include <resource.pb.h>
#include <grpcpp/grpcpp.h>
#include <obj_store.pb.h>
#include <obj_store.grpc.pb.h>


std::string obj_path(const std::string &id) {
    return "data/" + id;
}


__off_t seek_offset(int fd, int64_t offset) {
    if (offset > 0) {
        return lseek(fd, offset, SEEK_SET);
    } else if (offset < 0) { // -1 = file end
        return lseek(fd, offset+1, SEEK_END);
    }
    return 0;
}


class WriteHandler : public GRPCBasicHandler<
    obj_store::ObjStore::AsyncService, obj_store::WriteRequest, obj_store::Result, WriteHandler
> {
    using GRPCBasicHandler::GRPCBasicHandler;

    void bind(grpc::ServerCompletionQueue* cq) override {
        service->RequestWrite(&ctx, &request, &responder, cq, cq, this);
    }

    // TODO: error logging
    void handle_request() override {
        auto & id = request.id();
        auto & data = request.data();

        // TODO: auth
        // TODO: save object auth paths
        // TODO: report quota
        auto path = obj_path(id);
        unique_fd fd = open(path.c_str(), O_WRONLY | O_CREAT);
        if (!fd.valid()) {
            response.set_result(resource::OperationResult::FAILED);
            return;
        }
        if (seek_offset(fd, request.offset())) {
            response.set_result(resource::OperationResult::FAILED);
            return;
        }

        if (write(fd, data.data(), data.size()) != data.size()) {
            response.set_result(resource::OperationResult::FAILED);
            return;
        }

        response.set_result(resource::OperationResult::OK);
    }
};

class ReadHandler : public GRPCBasicHandler<
    obj_store::ObjStore::AsyncService,
    obj_store::ReadRequest,
    obj_store::ContentResult,
    ReadHandler
> {
    using GRPCBasicHandler::GRPCBasicHandler;
    void bind(grpc::ServerCompletionQueue* cq) override {
        service->RequestRead(&ctx, &request, &responder, cq, cq, this);
    }

    void handle_request() override {
        auto & id = request.id();

        // TODO: auth
        // TODO: object auth paths
        // TODO: report quota
        auto path = obj_path(id);
        unique_fd fd = open(path.c_str(), O_RDONLY);
        if (!fd.valid()) {
            response.set_result(resource::OperationResult::FAILED);
            return;
        }

        if (seek_offset(fd, request.offset())) {
            response.set_result(resource::OperationResult::FAILED);
            return;
        }

        auto to_read = request.data_len();
        if (to_read > 0) {
            std::string data(to_read, '\0');
            auto read_res = read(fd, data.data(), to_read);
            if (read_res <= 0) {
                response.set_result(resource::OperationResult::FAILED);
                return;
            }
            data.resize(read_res);
            response.set_content(data);
        }

        response.set_result(resource::OperationResult::OK);
    }
};

class DeleteHandler : public GRPCBasicHandler<
    obj_store::ObjStore::AsyncService,
    obj_store::DeleteRequest,
    obj_store::Result,
    DeleteHandler
> {
    using GRPCBasicHandler::GRPCBasicHandler;
    void bind(grpc::ServerCompletionQueue* cq) override {
        service->RequestDelete(&ctx, &request, &responder, cq, cq, this);
    }

    void handle_request() override {
        auto & id = request.id();

        // TODO: auth
        // TODO: object auth paths
        auto path = obj_path(id);
        if (unlink(path.c_str())) {
            response.set_result(resource::OperationResult::FAILED);
            return;
        }

        response.set_result(resource::OperationResult::OK);
    }
};


int main() {
    std::string server_address("0.0.0.0:50051");

    obj_store::ObjStore::AsyncService service;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<grpc::ServerCompletionQueue> cq = builder.AddCompletionQueue();
    auto server = builder.BuildAndStart();

    (new WriteHandler(&service))->process(cq.get(), true);
    (new ReadHandler(&service))->process(cq.get(), true);
    (new DeleteHandler(&service))->process(cq.get(), true);

    void* tag = nullptr;
    bool ok = true;
    while (cq->Next(&tag, &ok)) {
        static_cast<GRPCHandler*>(tag)->process(cq.get(), ok);
    }

    return 0;
}
