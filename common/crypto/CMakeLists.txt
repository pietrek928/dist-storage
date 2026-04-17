find_package(OpenSSL REQUIRED)

add_library(dist_storage_crypto STATIC
    sgn.cc
    call_check.cc
    auth.cc
    ssl.cc
    ../utils/exception.cc
)

target_include_directories(dist_storage_crypto PUBLIC "${CMAKE_SOURCE_DIR}/common")
target_link_libraries(dist_storage_crypto PUBLIC OpenSSL::SSL OpenSSL::Crypto)
