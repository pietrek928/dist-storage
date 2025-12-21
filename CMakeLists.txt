cmake_minimum_required(VERSION 3.10)
project(dist_storage CXX)

add_custom_target(proto
    COMMAND make -C ${CMAKE_SOURCE_DIR}/common/proto
    COMMENT "Project protos"
)
include_directories(${CMAKE_SOURCE_DIR}/common/proto/cc)
include_directories(${CMAKE_SOURCE_DIR}/common/)

add_subdirectory(minion/obj_store)
