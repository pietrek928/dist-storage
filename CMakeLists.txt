cmake_minimum_required(VERSION 3.13...4.2)
project(dist_storage CXX)

# Save commands for vscode clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
FetchContent_Declare(
  gRPC
  GIT_REPOSITORY https://github.com/grpc/grpc.git
  GIT_TAG        v1.75.1
)
# Use this instead of MakeAvailable to ensure variables are set before sub-add_subdirectory
FetchContent_GetProperties(gRPC)
if(NOT grpc_POPULATED)
  FetchContent_Populate(gRPC)
  add_subdirectory(${grpc_SOURCE_DIR} ${grpc_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()
####################


add_subdirectory(common/proto)
include_directories(${CMAKE_SOURCE_DIR}/common/)
include_directories(${CMAKE_SOURCE_DIR}/common/proto/cc)

add_subdirectory(common/grpc)
add_subdirectory(minion/obj_store)
