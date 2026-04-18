set(DIST_STORAGE_GRPC_SRCS
    addr.cc
    ssl_endpoint.cc
    resolver.cc
    engine.cc
    ../net/tcpv4.cc
    ../utils/sys/err.cc
    ../utils/sys/call_check.cc
    ../net/call_check.cc
    ../utils/poll_engine.cc
)

add_library(dist_storage_grpc STATIC ${DIST_STORAGE_GRPC_SRCS})

target_include_directories(dist_storage_grpc PRIVATE
    ${grpc_SOURCE_DIR}/include
    ${grpc_SOURCE_DIR}/src
    ${grpc_SOURCE_DIR}

    ${grpc_SOURCE_DIR}/third_party/upb
    ${grpc_SOURCE_DIR}/src/core/ext/upb-gen
    ${grpc_SOURCE_DIR}/src/core/ext/upbdefs-gen

    ${grpc_BINARY_DIR}
)

target_include_directories(dist_storage_grpc PUBLIC
    "${CMAKE_SOURCE_DIR}/common"
)

target_link_libraries(dist_storage_grpc PUBLIC
    grpc++
    absl::status
    absl::statusor
    absl::log
)
