# 1. Define Output Directories
set(CC_OUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cc")
set(PY_OUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/py")
set(GO_OUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/go")

# 2. Find all .proto files
file(GLOB PROTO_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.proto")

# 3. Locate plugins (more robust than hardcoded paths)
find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin)
find_program(GRPC_PYTHON_PLUGIN grpc_python_plugin)
# find_program(PROTOC_GEN_GO protoc-gen-go)

# 4. Create Generation Commands
add_custom_command(
    OUTPUT "${CC_OUT_DIR}" "${PY_OUT_DIR}" # "${GO_OUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CC_OUT_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PY_OUT_DIR}
    # COMMAND ${CMAKE_COMMAND} -E make_directory ${GO_OUT_DIR}

    # C++ & Python Generation
    COMMAND protoc --proto_path=${CMAKE_CURRENT_SOURCE_DIR}
            --cpp_out=${CC_OUT_DIR}
            --grpc_cpp_out=${CC_OUT_DIR}
            --plugin=protoc-gen-grpc_cpp=${GRPC_CPP_PLUGIN}
            --python_out=${PY_OUT_DIR}
            --grpc_python_out=${PY_OUT_DIR}
            --plugin=protoc-gen-grpc_python=${GRPC_PYTHON_PLUGIN}
            ${PROTO_FILES}

    # --- GO GENERATION (Commented out) ---
    # COMMAND protoc --proto_path=${CMAKE_CURRENT_SOURCE_DIR}
    #         --go_out=${GO_OUT_DIR}
    #         ${PROTO_FILES}
    # -------------------------------------

    DEPENDS ${PROTO_FILES}
    COMMENT "Generating C++, Python (and Go if enabled) code from Protocol Buffers"
    VERBATIM
)

# 5. Define the Top-Level Target
# Running 'make' will now trigger the generation logic
add_custom_target(generate_protos ALL DEPENDS "${CC_OUT_DIR}" "${PY_OUT_DIR}")

# 6. Cleaning
# Tells CMake to remove these folders when you run 'make clean'
set_directory_properties(PROPERTIES ADDITIONAL_MAKE_CLEAN_FILES "${CC_OUT_DIR};${PY_OUT_DIR};${GO_OUT_DIR}")