include_guard(GLOBAL)

set(OPENSSL_VERSION 3.3.2)
set(OPENSSL_URL "https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz")  # cmake-lint: disable=C0301

find_package(OpenSSL ${OPENSSL_VERSION})
if(NOT OpenSSL_FOUND OR NOT TARGET OpenSSL::Crypto)
    message(STATUS
            "OpenSSL v${OPENSSL_VERSION} package not found in the system or incomplete. Falling back to FetchContent.")
    include(FetchContent)

    # Avoid warning about DOWNLOAD_EXTRACT_TIMESTAMP in CMake 3.24:
    if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
        cmake_policy(SET CMP0135 NEW)
    endif()

    FetchContent_Declare(
            OpenSSL
            URL ${OPENSSL_URL}
            SYSTEM
            OVERRIDE_FIND_PACKAGE
    )

    FetchContent_MakeAvailable(openssl)

    set(OpenSSL_FOUND TRUE)  # cmake-lint: disable=C0103
    set(OPENSSL_INCLUDE_DIR ${OpenSSL_SOURCE_DIR}/include)
    set(OPENSSL_CRYPTO_LIBRARY ${OpenSSL_BINARY_DIR}/lib/libcrypto.a)
    set(OPENSSL_SSL_LIBRARY ${OpenSSL_BINARY_DIR}/lib/libssl.a)
    set(OPENSSL_LIBRARIES OpenSSL::Crypto OpenSSL::SSL)
endif()

message(STATUS "OpenSSL include dirs: ${OPENSSL_INCLUDE_DIR}")
message(STATUS "OpenSSL libraries: ${OPENSSL_LIBRARIES}")
