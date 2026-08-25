cmake_minimum_required(VERSION 3.16)
project(pqc_benchmark LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native")

# --- Common library (no crypto deps) ---
add_library(bench_common STATIC
    common/bench_common.cpp
)
target_include_directories(bench_common PUBLIC common)
target_link_libraries(bench_common PUBLIC pthread)

# --- OpenSSL 4.x backend (third_party) ---
set(OPENSSL4_ROOT "${CMAKE_SOURCE_DIR}/third_party/install/openssl4" CACHE PATH "OpenSSL 4.x install prefix")

if(EXISTS "${OPENSSL4_ROOT}/lib64/libcrypto.so" AND EXISTS "${OPENSSL4_ROOT}/include/openssl/evp.h")
    set(OPENSSL4_LIB_DIR "${OPENSSL4_ROOT}/lib64")
elseif(EXISTS "${OPENSSL4_ROOT}/lib/libcrypto.so" AND EXISTS "${OPENSSL4_ROOT}/include/openssl/evp.h")
    set(OPENSSL4_LIB_DIR "${OPENSSL4_ROOT}/lib")
else()
    set(OPENSSL4_LIB_DIR "")
endif()

if(OPENSSL4_LIB_DIR)
    add_executable(bench_openssl backends/openssl/bench_openssl.cpp)
    target_include_directories(bench_openssl PRIVATE "${OPENSSL4_ROOT}/include")
    target_link_libraries(bench_openssl PRIVATE bench_common "${OPENSSL4_LIB_DIR}/libcrypto.so" pthread)
    set_target_properties(bench_openssl PROPERTIES BUILD_RPATH "${OPENSSL4_LIB_DIR}")
    message(STATUS "OpenSSL 4.x found at ${OPENSSL4_ROOT} - bench_openssl enabled")
else()
    message(WARNING "OpenSSL 4.x not found at ${OPENSSL4_ROOT} - bench_openssl disabled")
endif()

# --- OpenSSL 3.x backend (third_party) ---
set(OPENSSL3_ROOT "${CMAKE_SOURCE_DIR}/third_party/install/openssl3" CACHE PATH "OpenSSL 3.x install prefix")

if(EXISTS "${OPENSSL3_ROOT}/lib64/libcrypto.so" AND EXISTS "${OPENSSL3_ROOT}/include/openssl/evp.h")
    set(OPENSSL3_LIB_DIR "${OPENSSL3_ROOT}/lib64")
elseif(EXISTS "${OPENSSL3_ROOT}/lib/libcrypto.so" AND EXISTS "${OPENSSL3_ROOT}/include/openssl/evp.h")
    set(OPENSSL3_LIB_DIR "${OPENSSL3_ROOT}/lib")
else()
    set(OPENSSL3_LIB_DIR "")
endif()

if(OPENSSL3_LIB_DIR)
    add_executable(bench_openssl3 backends/openssl/bench_openssl.cpp)
    target_include_directories(bench_openssl3 PRIVATE "${OPENSSL3_ROOT}/include")
    target_link_libraries(bench_openssl3 PRIVATE bench_common "${OPENSSL3_LIB_DIR}/libcrypto.so" pthread)
    set_target_properties(bench_openssl3 PROPERTIES BUILD_RPATH "${OPENSSL3_LIB_DIR}")
    message(STATUS "OpenSSL 3.x found at ${OPENSSL3_ROOT} - bench_openssl3 enabled")
else()
    message(WARNING "OpenSSL 3.x not found at ${OPENSSL3_ROOT} - bench_openssl3 disabled")
endif()

# --- BoringSSL backend (third_party shared build) ---
set(BORINGSSL_ROOT "${CMAKE_SOURCE_DIR}/third_party/install/boringssl"
    CACHE PATH "BoringSSL install prefix")
set(BORINGSSL_VERSION "0.20260803.0" CACHE STRING "BoringSSL version tag")

if(EXISTS "${BORINGSSL_ROOT}/lib/libcrypto.so" AND EXISTS "${BORINGSSL_ROOT}/include/openssl/evp.h")
    add_executable(bench_boringssl backends/boringssl/bench_boringssl.cpp)
    target_include_directories(bench_boringssl PRIVATE "${BORINGSSL_ROOT}/include")
    target_compile_definitions(bench_boringssl PRIVATE BENCH_BORINGSSL_VERSION="${BORINGSSL_VERSION}")
    target_link_libraries(bench_boringssl PRIVATE
        bench_common
        "${BORINGSSL_ROOT}/lib/libcrypto.so"
        pthread
    )
    set_target_properties(bench_boringssl PROPERTIES BUILD_RPATH "${BORINGSSL_ROOT}/lib")
    message(STATUS "BoringSSL ${BORINGSSL_VERSION} found at ${BORINGSSL_ROOT} - bench_boringssl enabled")
else()
    message(WARNING "BoringSSL not found at ${BORINGSSL_ROOT} - bench_boringssl disabled. Run scripts/build-deps.sh first.")
endif()

# --- AWS-LC backend (third_party shared build) ---
set(AWS_LC_ROOT "${CMAKE_SOURCE_DIR}/third_party/install/aws-lc"
    CACHE PATH "AWS-LC install prefix")

if(EXISTS "${AWS_LC_ROOT}/lib/libcrypto.so")
    set(AWS_LC_CRYPTO_LIB "${AWS_LC_ROOT}/lib/libcrypto.so")
    set(AWS_LC_LIB_DIR "${AWS_LC_ROOT}/lib")
elseif(EXISTS "${AWS_LC_ROOT}/lib64/libcrypto.so")
    set(AWS_LC_CRYPTO_LIB "${AWS_LC_ROOT}/lib64/libcrypto.so")
    set(AWS_LC_LIB_DIR "${AWS_LC_ROOT}/lib64")
else()
    set(AWS_LC_CRYPTO_LIB "")
    set(AWS_LC_LIB_DIR "")
endif()

if(AWS_LC_CRYPTO_LIB AND EXISTS "${AWS_LC_ROOT}/include/openssl/evp.h")
    add_executable(bench_aws_lc backends/aws_lc/bench_aws_lc.cpp)
    target_include_directories(bench_aws_lc PRIVATE "${AWS_LC_ROOT}/include")
    target_link_libraries(bench_aws_lc PRIVATE
        bench_common
        "${AWS_LC_CRYPTO_LIB}"
        pthread dl
    )
    set_target_properties(bench_aws_lc PROPERTIES BUILD_RPATH "${AWS_LC_LIB_DIR}")
    message(STATUS "AWS-LC found at ${AWS_LC_ROOT} - bench_aws_lc enabled")
else()
    message(WARNING "AWS-LC not found at ${AWS_LC_ROOT} - bench_aws_lc disabled. Run scripts/build-deps.sh first.")
endif()

# --- GnuTLS backend (third_party) ---
set(GNUTLS_ROOT "${CMAKE_SOURCE_DIR}/third_party/install/gnutls" CACHE PATH "GnuTLS install prefix")
set(LEANCRYPTO_ROOT "${CMAKE_SOURCE_DIR}/third_party/install/leancrypto" CACHE PATH "leancrypto install prefix")

if(EXISTS "${GNUTLS_ROOT}/lib/libgnutls.so" AND EXISTS "${GNUTLS_ROOT}/include/gnutls/gnutls.h")
    set(GNUTLS_LIB_DIR "${GNUTLS_ROOT}/lib")
elseif(EXISTS "${GNUTLS_ROOT}/lib64/libgnutls.so" AND EXISTS "${GNUTLS_ROOT}/include/gnutls/gnutls.h")
    set(GNUTLS_LIB_DIR "${GNUTLS_ROOT}/lib64")
else()
    set(GNUTLS_LIB_DIR "")
endif()

if(EXISTS "${LEANCRYPTO_ROOT}/lib64/libleancrypto.so")
    set(LEANCRYPTO_LIB_DIR "${LEANCRYPTO_ROOT}/lib64")
elseif(EXISTS "${LEANCRYPTO_ROOT}/lib/libleancrypto.so")
    set(LEANCRYPTO_LIB_DIR "${LEANCRYPTO_ROOT}/lib")
else()
    set(LEANCRYPTO_LIB_DIR "")
endif()

if(GNUTLS_LIB_DIR)
    add_executable(bench_gnutls backends/gnutls/bench_gnutls.cpp)
    target_include_directories(bench_gnutls PRIVATE "${GNUTLS_ROOT}/include")
    target_link_libraries(bench_gnutls PRIVATE bench_common "${GNUTLS_LIB_DIR}/libgnutls.so" pthread)
    set(GNUTLS_RPATH "${GNUTLS_LIB_DIR}")
    if(LEANCRYPTO_LIB_DIR)
        string(APPEND GNUTLS_RPATH ";${LEANCRYPTO_LIB_DIR}")
    endif()
    set_target_properties(bench_gnutls PROPERTIES BUILD_RPATH "${GNUTLS_RPATH}")
    # Use DT_RPATH (propagates to transitive deps) instead of DT_RUNPATH
    target_link_options(bench_gnutls PRIVATE "LINKER:--disable-new-dtags")
    message(STATUS "GnuTLS found at ${GNUTLS_ROOT} - bench_gnutls enabled")
else()
    message(WARNING "GnuTLS not found at ${GNUTLS_ROOT} - bench_gnutls disabled. Run scripts/build-deps.sh first.")
endif()
