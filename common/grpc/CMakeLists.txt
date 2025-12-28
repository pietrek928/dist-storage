# Define an Object Library (outputs .o files)
add_library(grpc_transport_obj OBJECT
    ssl_endpoint.cc
)

# Apply internal header paths to these object files
target_include_directories(grpc_transport_obj PRIVATE
    ${grpc_SOURCE_DIR}/src
    ${grpc_BINARY_DIR}
)

# You still need to link dependencies so the .o knows about the types
target_link_libraries(grpc_transport_obj PRIVATE
    grpc++
    absl_status
)