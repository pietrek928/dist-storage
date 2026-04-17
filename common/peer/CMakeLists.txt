add_library(dist_storage_peer STATIC
    message.cc
    store.cc
    ../utils/sys/time.cc
)

target_include_directories(dist_storage_peer PRIVATE
    ${grpc_SOURCE_DIR}/include
    ${grpc_BINARY_DIR}
)

target_include_directories(dist_storage_peer PUBLIC
    "${CMAKE_SOURCE_DIR}/common"
)

target_link_libraries(dist_storage_peer PUBLIC
    dist_storage_crypto
    grpc++
)
