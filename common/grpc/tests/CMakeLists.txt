add_executable(dist_storage_grpc_tests
    addr_test.cc
    ref_counted_arg_test.cc
    callback_test.cc
    loop_test.cc
    ssl_endpoint_test.cc
)

target_include_directories(dist_storage_grpc_tests PRIVATE
    ${grpc_SOURCE_DIR}/include
    ${grpc_SOURCE_DIR}/src
    ${grpc_SOURCE_DIR}
    ${grpc_BINARY_DIR}
)

target_link_libraries(dist_storage_grpc_tests PRIVATE
    dist_storage_grpc
    GTest::gtest_main
)

gtest_discover_tests(dist_storage_grpc_tests
    TEST_PREFIX "dist_storage_grpc."
)
