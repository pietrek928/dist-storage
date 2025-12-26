cmake_minimum_required(VERSION 3.13...4.2)
project(dist_storage CXX)

add_subdirectory(common/proto)
include_directories(${CMAKE_SOURCE_DIR}/common/proto/cc)
include_directories(${CMAKE_SOURCE_DIR}/common/)

add_subdirectory(common/grpc)
add_subdirectory(minion/obj_store)
