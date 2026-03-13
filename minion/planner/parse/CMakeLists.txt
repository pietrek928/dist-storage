# ------------------------------------------------------------------
# PART 1: Define the Single Source of Truth for Versioning
# ------------------------------------------------------------------
set(ANTLR_VERSION "4.13.2")

# ------------------------------------------------------------------
# PART 2: Fetch matching C++ Runtime Source
# ------------------------------------------------------------------
include(FetchContent)

# Disable ANTLR's internal tests and shared libs to speed up your build
set(ANTLR_BUILD_CPP_TESTS OFF CACHE BOOL "" FORCE)
set(ANTLR_BUILD_SHARED OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    antlr4_runtime
    GIT_REPOSITORY https://github.com/antlr/antlr4.git
    GIT_TAG        ${ANTLR_VERSION}
    SOURCE_SUBDIR  runtime/Cpp
)

message(STATUS "Fetching ANTLR ${ANTLR_VERSION} C++ Runtime...")
FetchContent_MakeAvailable(antlr4_runtime)

# ------------------------------------------------------------------
# PART 3: Download matching Generator JAR
# ------------------------------------------------------------------
set(ANTLR_JAR "${CMAKE_CURRENT_BINARY_DIR}/antlr-${ANTLR_VERSION}-complete.jar")
if(NOT EXISTS "${ANTLR_JAR}")
    message(STATUS "Downloading ANTLR ${ANTLR_VERSION} Java Generator...")
    file(DOWNLOAD
        "https://www.antlr.org/download/antlr-${ANTLR_VERSION}-complete.jar"
        "${ANTLR_JAR}"
        SHOW_PROGRESS
    )
endif()

# ------------------------------------------------------------------
# PART 2: Generate C++ Code from the .g4 Grammar
# ------------------------------------------------------------------
set(GRAMMAR_FILE "${CMAKE_CURRENT_SOURCE_DIR}/grammar/sql.g4")
set(GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated")

add_custom_command(
    OUTPUT
        "${GENERATED_DIR}/sqlLexer.cpp"
        "${GENERATED_DIR}/sqlLexer.h"
        "${GENERATED_DIR}/sqlParser.cpp"
        "${GENERATED_DIR}/sqlParser.h"
    COMMAND java -jar "${ANTLR_JAR}" -Dlanguage=Cpp -no-listener -visitor -o "${GENERATED_DIR}" "${GRAMMAR_FILE}"
    DEPENDS "${GRAMMAR_FILE}"
    COMMENT "Generating C++ parser code from system ANTLR4..."
)

# ------------------------------------------------------------------
# PART 3: Build Your Object Files
# ------------------------------------------------------------------
# CHANGED: Using add_library with the OBJECT keyword instead of add_executable
# add_library(query_parser_obj OBJECT
#     "parse_query.cc"
#     "${GENERATED_DIR}/sqlLexer.cpp"
#     "${GENERATED_DIR}/sqlParser.cpp"
# )
add_executable(query_parser_obj  # executable for debug
    "parse_query.cc"
    "${GENERATED_DIR}/sqlLexer.cpp"
    "${GENERATED_DIR}/sqlParser.cpp"
)

# Tell the object targets where to find the generated headers AND the missing ANTLR headers
target_include_directories(query_parser_obj PRIVATE
    "${GENERATED_DIR}"
    "${ANTLR4_INCLUDE_DIR}"           # Variable exposed by older ANTLR cmake configs
)

# Link against the static target to ensure ANTLR headers are found during compilation
# Note: Using PUBLIC here so whatever links against these objects later also knows about ANTLR
# target_link_libraries(query_parser_obj PUBLIC antlr4_static)
target_link_libraries(query_parser_obj PUBLIC antlr4_static)
target_link_libraries(query_parser_obj PRIVATE
    grpc++
    my_proto_lib
)
