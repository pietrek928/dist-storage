add_executable(obj_store
    obj_store.cc
    "${CMAKE_SOURCE_DIR}/common/utils/sys/err.cc"
)

# dist_storage_common: <utils/...>, <grpc/callback.h> (project tree under common/)
# my_proto_lib: generated *.pb.h under common/proto/cc + libprotobuf/grpc++
# gRPC static lib pulls TLS code that needs OpenSSL symbols at link time.
find_package(OpenSSL REQUIRED)
target_link_libraries(obj_store PRIVATE
    dist_storage_common
    my_proto_lib
    OpenSSL::SSL
    OpenSSL::Crypto
)
