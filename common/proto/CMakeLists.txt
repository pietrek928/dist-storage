# 1. Define Output Directories
set(CC_OUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cc")
set(PY_OUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/py")

# 2. Find all .proto files
file(GLOB PROTO_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.proto")

# ---------------------------------------------------------
# NEW: 2.5 Predict the generated C++ filenames
# ---------------------------------------------------------
set(PROTO_CC_SRCS "")
set(PROTO_CC_HDRS "")
foreach(FIL ${PROTO_FILES})
    # Extract just the filename without the extension (e.g., "message" from "message.proto")
    get_filename_component(FIL_WE ${FIL} NAME_WE)

    # Predict the Protobuf files
    list(APPEND PROTO_CC_SRCS "${CC_OUT_DIR}/${FIL_WE}.pb.cc")
    list(APPEND PROTO_CC_HDRS "${CC_OUT_DIR}/${FIL_WE}.pb.h")

    # Predict the gRPC files
    list(APPEND PROTO_CC_SRCS "${CC_OUT_DIR}/${FIL_WE}.grpc.pb.cc")
    list(APPEND PROTO_CC_HDRS "${CC_OUT_DIR}/${FIL_WE}.grpc.pb.h")
endforeach()

# 3. Locate plugins
set(PROTOC $<TARGET_FILE:protoc>)
set(GRPC_CPP_PLUGIN $<TARGET_FILE:grpc_cpp_plugin>)
set(GRPC_PYTHON_PLUGIN $<TARGET_FILE:grpc_python_plugin>)

set(PROTOBUF_WELL_KNOWN_INC_DIR "${grpc_SOURCE_DIR}/third_party/protobuf/src")

# 4. Create Generation Commands
add_custom_command(
    # NEW: Output the exact predicted files, not just the directory!
    # This guarantees CMake knows exactly what to wait for before compiling.
    OUTPUT ${PROTO_CC_SRCS} ${PROTO_CC_HDRS}

    COMMAND ${CMAKE_COMMAND} -E make_directory ${CC_OUT_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PY_OUT_DIR}

    COMMAND ${PROTOC}
            --proto_path=${CMAKE_CURRENT_SOURCE_DIR}
            --proto_path=${PROTOBUF_WELL_KNOWN_INC_DIR} # Added local headers path
            --cpp_out=${CC_OUT_DIR}
            --grpc_cpp_out=${CC_OUT_DIR}
            --plugin=protoc-gen-grpc_cpp=${GRPC_CPP_PLUGIN}
            --python_out=${PY_OUT_DIR}
            --grpc_python_out=${PY_OUT_DIR}
            --plugin=protoc-gen-grpc_python=${GRPC_PYTHON_PLUGIN}
            ${PROTO_FILES}

    DEPENDS ${PROTO_FILES} protoc grpc_cpp_plugin
    COMMENT "Generating C++ code using LOCAL compiled gRPC"
    VERBATIM
)

# ---------------------------------------------------------
# NEW: 5. Compile the generated files into a C++ Library
# ---------------------------------------------------------
# Create a static library containing your compiled protobuf code
add_library(my_proto_lib STATIC ${PROTO_CC_SRCS} ${PROTO_CC_HDRS})

# Tell the library where to find its own generated headers
target_include_directories(my_proto_lib PUBLIC ${CC_OUT_DIR})

# Link the required Protobuf and gRPC core libraries
target_link_libraries(my_proto_lib PUBLIC
    libprotobuf
    grpc++
)

# 6. Cleaning
set_directory_properties(PROPERTIES ADDITIONAL_MAKE_CLEAN_FILES "${CC_OUT_DIR};${PY_OUT_DIR}")
