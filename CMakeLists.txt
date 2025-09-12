cmake_minimum_required(VERSION 3.10)
project(dist_storage)

add_custom_target(proto
    COMMAND make -C ${CMAKE_SOURCE_DIR}/common/proto
    COMMENT "Project protos"
)
