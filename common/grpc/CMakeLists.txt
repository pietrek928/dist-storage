# 1. Settings to fix compilation errors
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
set(gRPC_INSTALL OFF CACHE BOOL "" FORCE)
set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
set(utf8_range_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

# 2. Force gRPC to build the C++ stack
set(gRPC_BUILD_CSHARP_EXT OFF CACHE BOOL "" FORCE)
set(gRPC_BUILD_GRPC_CPP ON CACHE BOOL "" FORCE)
set(gRPC_BUILD_GRPC_CSHARP OFF CACHE BOOL "" FORCE)
set(gRPC_BUILD_GRPC_PHP OFF CACHE BOOL "" FORCE)
set(gRPC_BUILD_GRPC_PYTHON OFF CACHE BOOL "" FORCE)
set(gRPC_BUILD_GRPC_RUBY OFF CACHE BOOL "" FORCE)

# 3. Handle SSL - Since you use LibreSSL, tell gRPC to look for it or use "package"
# If you haven't found LibreSSL yet, gRPC might be failing to create grpc++
set(gRPC_SSL_PROVIDER "package" CACHE STRING "" FORCE)

# 1. Fetch gRPC (Standard way)
include(FetchContent)
FetchContent_Declare(
  gRPC
  GIT_REPOSITORY https://github.com/grpc/grpc.git
  GIT_TAG        v1.66.0 # Use a stable version!
)
# Use this instead of MakeAvailable to ensure variables are set before sub-add_subdirectory
FetchContent_GetProperties(gRPC)
if(NOT grpc_POPULATED)
  FetchContent_Populate(gRPC)
  add_subdirectory(${grpc_SOURCE_DIR} ${grpc_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

# Define an Object Library (outputs .o files)
add_library(grpc_transport_obj OBJECT
    ssl_endpoint.cc
)

# Apply internal header paths to these object files
target_include_directories(grpc_transport_obj PRIVATE
    ${grpc_SOURCE_DIR}
    ${grpc_BINARY_DIR}
)

# You still need to link dependencies so the .o knows about the types
target_link_libraries(grpc_transport_obj PRIVATE
    grpc++
    absl_status
)