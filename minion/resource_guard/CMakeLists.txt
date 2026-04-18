add_executable(resource_guard
    resource_guard.cc
)

# dist_storage_common: <grpc/callback.h> (project tree under common/)
# my_proto_lib: generated *.pb.h under common/proto/cc + libprotobuf/grpc++
# gRPC static lib pulls TLS code that needs OpenSSL symbols at link time.
find_package(OpenSSL REQUIRED)
target_link_libraries(resource_guard PRIVATE
    dist_storage_common
    my_proto_lib
    OpenSSL::SSL
    OpenSSL::Crypto
)
