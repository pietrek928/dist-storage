cmake_minimum_required(VERSION 4.0)
project(dist_storage CXX)

# Save commands for vscode clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Shared include root for project headers (<utils/...>, <grpc/...>, etc.)
add_library(dist_storage_common INTERFACE)
target_include_directories(dist_storage_common INTERFACE "${CMAKE_SOURCE_DIR}/common")

################ GRPC
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
# set(gRPC_SSL_PROVIDER "package" CACHE STRING "" FORCE)
set(gRPC_SSL_PROVIDER libreSSL CACHE STRING "" FORCE)

# 4. Fetch gRPC (Standard way)
include(FetchContent)
get_filename_component(_grpc_src "${FETCHCONTENT_BASE_DIR}/grpc-src" ABSOLUTE)
FetchContent_Declare(
  gRPC
  GIT_REPOSITORY https://github.com/grpc/grpc.git
  GIT_TAG        v1.75.1
)
FetchContent_MakeAvailable(gRPC)
####################


add_subdirectory(common/crypto)
add_subdirectory(common/proto)
include_directories(${CMAKE_SOURCE_DIR}/common)
include_directories(${CMAKE_SOURCE_DIR}/common/proto/cc)

add_subdirectory(common/grpc)
add_subdirectory(common/peer)
add_subdirectory(minion/obj_store)
add_subdirectory(minion/resource_guard)
add_subdirectory(minion/message)
add_subdirectory(minion/planner/parse)
