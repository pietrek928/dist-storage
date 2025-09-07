include(FindProtobuf)

find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

include_directories(${PROTOBUF_INCLUDE_DIR})
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS auth.proto obj_store.proto)
grpc_generate_cpp(GRPC_SRCS GRPC_HDRS obj_store.proto)
add_library(proto ${PROTO_HEADER} ${PROTO_SRC})
