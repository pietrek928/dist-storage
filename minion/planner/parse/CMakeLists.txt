# ------------------------------------------------------------------
# Single source of truth for ANTLR tool + C++ runtime
# ------------------------------------------------------------------
set(ANTLR_VERSION "4.13.2")
set(ANTLR_JAR_SHA256 "eae2dfa119a64327444672aff63e9ec35a20180dc5b8090b7a6ab85125df4d76")

find_package(Java COMPONENTS Runtime REQUIRED)

include(FetchContent)

set(ANTLR_BUILD_CPP_TESTS OFF CACHE BOOL "" FORCE)
set(ANTLR_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(DISABLE_WARNINGS ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    antlr4_runtime
    GIT_REPOSITORY https://github.com/antlr/antlr4.git
    GIT_TAG        ${ANTLR_VERSION}
    SOURCE_SUBDIR  runtime/Cpp
)

message(STATUS "Fetching ANTLR ${ANTLR_VERSION} C++ Runtime...")
FetchContent_MakeAvailable(antlr4_runtime)

# Treat ANTLR headers as system so upstream [[deprecated]] attributes do not
# warn through into project translation units.
get_target_property(_antlr4_inc_dirs antlr4_static INTERFACE_INCLUDE_DIRECTORIES)
if(_antlr4_inc_dirs)
    set_property(TARGET antlr4_static PROPERTY
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_antlr4_inc_dirs}")
endif()

# ------------------------------------------------------------------
# Matching generator JAR (integrity-checked)
# ------------------------------------------------------------------
set(ANTLR_JAR "${CMAKE_CURRENT_BINARY_DIR}/antlr-${ANTLR_VERSION}-complete.jar")
message(STATUS "Ensuring ANTLR ${ANTLR_VERSION} Java generator JAR...")
file(DOWNLOAD
    "https://www.antlr.org/download/antlr-${ANTLR_VERSION}-complete.jar"
    "${ANTLR_JAR}"
    EXPECTED_HASH SHA256=${ANTLR_JAR_SHA256}
    SHOW_PROGRESS
)

# ------------------------------------------------------------------
# Generate C++ from grammar into the build tree
# ------------------------------------------------------------------
set(GRAMMAR_FILE "${CMAKE_CURRENT_SOURCE_DIR}/grammar/sql.g4")
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(ANTLR_GEN_STAMP "${GENERATED_DIR}/.stamp")

add_custom_command(
    OUTPUT "${ANTLR_GEN_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GENERATED_DIR}"
    COMMAND ${Java_JAVA_EXECUTABLE} -jar "${ANTLR_JAR}"
        -Dlanguage=Cpp -no-listener -visitor
        -o "${GENERATED_DIR}" "${GRAMMAR_FILE}"
    COMMAND ${CMAKE_COMMAND} -E touch "${ANTLR_GEN_STAMP}"
    DEPENDS "${GRAMMAR_FILE}" "${ANTLR_JAR}"
    COMMENT "Generating C++ parser from sql.g4 (ANTLR ${ANTLR_VERSION})"
    VERBATIM
)

add_custom_target(sql_antlr_gen DEPENDS "${ANTLR_GEN_STAMP}")

# Ensure generated sources exist at configure time so GLOB can see them.
if(NOT EXISTS "${ANTLR_GEN_STAMP}")
    message(STATUS "Running ANTLR ${ANTLR_VERSION} codegen (configure)...")
    file(MAKE_DIRECTORY "${GENERATED_DIR}")
    execute_process(
        COMMAND ${Java_JAVA_EXECUTABLE} -jar "${ANTLR_JAR}"
            -Dlanguage=Cpp -no-listener -visitor
            -o "${GENERATED_DIR}" "${GRAMMAR_FILE}"
        RESULT_VARIABLE _antlr_gen_rc
        ERROR_VARIABLE _antlr_gen_err
    )
    if(NOT _antlr_gen_rc EQUAL 0)
        message(FATAL_ERROR "ANTLR codegen failed: ${_antlr_gen_err}")
    endif()
    file(TOUCH "${ANTLR_GEN_STAMP}")
endif()

file(GLOB ANTLR_GENERATED_SOURCES CONFIGURE_DEPENDS "${GENERATED_DIR}/*.cpp")
file(GLOB ANTLR_GENERATED_HEADERS CONFIGURE_DEPENDS "${GENERATED_DIR}/*.h")

add_executable(query_parser_obj
    parse_query.cc
    ${ANTLR_GENERATED_SOURCES}
)
add_dependencies(query_parser_obj sql_antlr_gen)

# Headers are consumed via the include path; listing them keeps IDEs/rebuilds aware.
target_sources(query_parser_obj PRIVATE ${ANTLR_GENERATED_HEADERS})
set_source_files_properties(${ANTLR_GENERATED_HEADERS} PROPERTIES HEADER_FILE_ONLY TRUE)

target_include_directories(query_parser_obj PRIVATE
    "${GENERATED_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_compile_features(query_parser_obj PRIVATE cxx_std_17)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(query_parser_obj PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-Wdeprecated-declarations>
        $<$<COMPILE_LANGUAGE:CXX>:-Werror=deprecated-declarations>
    )
endif()

# Suppress noisy warnings in ANTLR-generated lexer/parser TUs only.
set_source_files_properties(${ANTLR_GENERATED_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-Wno-error=deprecated-declarations;-Wno-unused-parameter"
)

target_link_libraries(query_parser_obj PRIVATE
    antlr4_static
    grpc++
    my_proto_lib
)
