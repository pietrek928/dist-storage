#include <utils/unique_fd.h>
#include <grpc/callback.h>
#include <crypto/sgn.h>

#include <algorithm>
#include <memory>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

#include <resource.pb.h>
#include <grpcpp/grpcpp.h>
#include <obj_store.pb.h>
#include <obj_store.grpc.pb.h>


std::string obj_path(const std::string &id) {
    return "data/" + id;
}

// offset < 0: seek relative to end (proto: -1 = end of file uses offset+1 in SEEK_END).
bool seek_ok(int fd, int64_t offset) {
    if (offset > 0) {
        return lseek(fd, static_cast<off_t>(offset), SEEK_SET) != static_cast<off_t>(-1);
    }
    if (offset < 0) {
        return lseek(fd, static_cast<off_t>(offset + 1), SEEK_END) != static_cast<off_t>(-1);
    }
    return true;
}

static const char *hash_digest_name(obj_store::HashType t) {
    switch (t) {
        case obj_store::SHA256:
            return "sha256";
        case obj_store::SHA512:
            return "sha512";
        case obj_store::SHA3_256:
            return "sha3-256";
        case obj_store::SHA3_512:
            return "sha3-512";
        default:
            return "sha256";
    }
}


class WriteHandler : public GRPCBasicHandler<
    WriteHandler,
    obj_store::ObjStore::AsyncService,
    obj_store::WriteRequest,
    obj_store::Result
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
        unique_fd fd = open(path.c_str(), O_WRONLY | O_CREAT, 0644);
        if (!fd.valid()) {
            response.set_result(resource::OperationResult::FAILED);
            return;
        }
        if (!seek_ok(fd, request.offset())) {
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
    ReadHandler,
    obj_store::ObjStore::AsyncService,
    obj_store::ReadRequest,
    obj_store::ContentResult
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

        if (!seek_ok(fd, request.offset())) {
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
    DeleteHandler,
    obj_store::ObjStore::AsyncService,
    obj_store::DeleteRequest,
    obj_store::Result
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

class HashHandler : public GRPCBasicHandler<
    HashHandler,
    obj_store::ObjStore::AsyncService,
    obj_store::HashRequest,
    obj_store::ContentResult
> {
    using GRPCBasicHandler::GRPCBasicHandler;

    void bind(grpc::ServerCompletionQueue *cq) override {
        service->RequestHash(&ctx, &request, &responder, cq, cq, this);
    }

    void handle_request() override {
        try {
            // TODO: auth
            // TODO: object auth paths
            // TODO: report quota
            auto path = obj_path(request.id());
            unique_fd fd = open(path.c_str(), O_RDONLY);
            if (!fd.valid()) {
                response.set_result(resource::OperationResult::FAILED);
                return;
            }
            if (!seek_ok(fd, request.offset())) {
                response.set_result(resource::OperationResult::FAILED);
                return;
            }

            SSLHasher hasher(hash_digest_name(request.hash_type()));
            hasher.start();

            constexpr size_t kChunk = 65536;
            std::vector<byte_t> buf(kChunk);
            const int64_t data_len = request.data_len();

            if (data_len > 0) {
                int64_t remaining = data_len;
                while (remaining > 0) {
                    const size_t want = static_cast<size_t>(
                        std::min<int64_t>(remaining, static_cast<int64_t>(kChunk)));
                    const ssize_t n = read(fd, buf.data(), want);
                    if (n <= 0) {
                        response.set_result(resource::OperationResult::FAILED);
                        return;
                    }
                    hasher.put(buf.data(), static_cast<size_t>(n));
                    remaining -= n;
                }
            } else {
                for (;;) {
                    const ssize_t n = read(fd, buf.data(), buf.size());
                    if (n < 0) {
                        response.set_result(resource::OperationResult::FAILED);
                        return;
                    }
                    if (n == 0) {
                        break;
                    }
                    hasher.put(buf.data(), static_cast<size_t>(n));
                }
            }

            std::vector<byte_t> out(static_cast<size_t>(hasher.digest_size()));
            const size_t out_len = hasher.finish(out.data());
            response.set_content(
                std::string(reinterpret_cast<const char *>(out.data()), out_len));
            response.set_result(resource::OperationResult::OK);
        } catch (...) {
            response.set_result(resource::OperationResult::FAILED);
        }
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

    grpc_prime_async_handler(std::make_unique<WriteHandler>(&service), cq.get(), true);
    grpc_prime_async_handler(std::make_unique<ReadHandler>(&service), cq.get(), true);
    grpc_prime_async_handler(std::make_unique<DeleteHandler>(&service), cq.get(), true);
    grpc_prime_async_handler(std::make_unique<HashHandler>(&service), cq.get(), true);

    void* tag = nullptr;
    bool ok = true;
    while (cq->Next(&tag, &ok)) {
        static_cast<GRPCHandler*>(tag)->process(cq.get(), ok);
        grpc_run_deferred_handler_destroys();
    }

    return 0;
}
