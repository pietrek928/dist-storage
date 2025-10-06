#include <unique_fd.h>
#include <task_runner.h>
#include <grpc_callback.h>

#include <fcntl.h>
#include <unistd.h>

#include <auth.pb.h>
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


class WriteHandler : public GRPCSimpleHandler<
    obj_store::ObjStore::AsyncService, obj_store::WriteRequest, obj_store::Result
> {
    // TODO: error logging
    void handle_request() {
        auto & id = request.id();
        auto & data = request.data();

        // TODO: auth
        // TODO: save object auth paths
        // TODO: report quota
        auto path = obj_path(id);
        unique_fd fd = open(path.c_str(), O_WRONLY | O_CREAT);
        if (!fd.valid()) {
            response.set_result(auth::OperationResult::FAILED);
            return;
        }
        if (seek_offset(fd, request.offset())) {
            response.set_result(auth::OperationResult::FAILED);
            return;
        }

        if (write(fd, data.data(), data.size()) != data.size()) {
            response.set_result(auth::OperationResult::FAILED);
            return;
        }

        response.set_result(auth::OperationResult::OK);
    }
};

class ReadHandler : public GRPCSimpleHandler<
    obj_store::ObjStore::AsyncService, obj_store::ReadRequest, obj_store::ContentResult
> {
    void handle_request() {
        auto & id = request.id();

        // TODO: auth
        // TODO: object auth paths
        // TODO: report quota
        auto path = obj_path(id);
        unique_fd fd = open(path.c_str(), O_RDONLY);
        if (!fd.valid()) {
            response.set_result(auth::OperationResult::FAILED);
            return;
        }

        if (seek_offset(fd, request.offset())) {
            response.set_result(auth::OperationResult::FAILED);
            return;
        }

        auto to_read = request.data_len();
        if (to_read > 0) {
            std::string data(to_read, '\0');
            auto read_res = read(fd, data.data(), to_read);
            if (read_res <= 0) {
                response.set_result(auth::OperationResult::FAILED);
                return;
            }
            data.resize(read_res);
            response.set_content(data);
        }

        response.set_result(auth::OperationResult::OK);
    }
};

class DeleteHandler : public GRPCSimpleHandler<
    obj_store::ObjStore::AsyncService, obj_store::DeleteRequest, obj_store::Result
> {
    void handle_request() {
        auto & id = request.id();

        // TODO: auth
        // TODO: object auth paths
        auto path = obj_path(id);
        if (unlink(path.c_str())) {
            response.set_result(auth::OperationResult::FAILED);
            return;
        }

        response.set_result(auth::OperationResult::OK);
    }
};


int main() {
    std::string server_address("0.0.0.0:50051");

    obj_store::ObjStore::AsyncService service;
    grpc::CompletionQueue cq;

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    auto server = builder.BuildAndStart();
    TaskRunner runner(2);

    service.RequestRead();

    return 0;
}
