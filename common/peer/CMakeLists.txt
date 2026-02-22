# Define an Object Library (outputs .o files)
add_library(peer_obj OBJECT
    message.cc
    store.cc
)

# Apply internal header paths to these object files
target_include_directories(peer_obj PRIVATE
    ${grpc_SOURCE_DIR}/include
    ${grpc_BINARY_DIR}
)

# You still need to link dependencies so the .o knows about the types
target_link_libraries(peer_obj PRIVATE
    grpc++
)