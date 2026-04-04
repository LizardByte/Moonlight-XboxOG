include_guard(GLOBAL)

include(ExternalProject)

set(OPENSSL_VERSION 1.1.1w)
set(OPENSSL_SOURCE_DIR "${CMAKE_SOURCE_DIR}/third-party/openssl")
set(OPENSSL_BUILD_ROOT "${CMAKE_BINARY_DIR}/third-party/openssl")
set(OPENSSL_BUILD_DIR "${OPENSSL_BUILD_ROOT}/build")
set(OPENSSL_INSTALL_DIR "${OPENSSL_BUILD_ROOT}/install")

if(MOONLIGHT_BUILD_KIND STREQUAL "XBOX")
    set(MOONLIGHT_OPENSSL_PLATFORM "XBOX")
else()
    set(MOONLIGHT_OPENSSL_PLATFORM "HOST")
endif()

file(MAKE_DIRECTORY "${OPENSSL_BUILD_DIR}")
file(MAKE_DIRECTORY "${OPENSSL_INSTALL_DIR}/include")
file(MAKE_DIRECTORY "${OPENSSL_INSTALL_DIR}/lib")

if(NOT EXISTS "${OPENSSL_SOURCE_DIR}/Configure")
    message(FATAL_ERROR
            "OpenSSL submodule not found at ${OPENSSL_SOURCE_DIR}. Run: git submodule update --init --recursive")
endif()

find_program(PERL_EXECUTABLE perl REQUIRED)
set(OPENSSL_CONFIGURE_OPTIONS
        no-shared
        no-tests
        no-asm
        no-comp
        no-threads
        no-afalgeng
        no-capieng
        no-ui-console
        no-ocsp
        no-srp
        no-pic
        no-async
        no-dso)

set(OPENSSL_ENV ${CMAKE_COMMAND} -E env)

if(MOONLIGHT_OPENSSL_PLATFORM STREQUAL "XBOX")
    find_program(OPENSSL_MAKE_EXECUTABLE NAMES make REQUIRED)

    set(OPENSSL_CONFIGURE_TARGET linux-x86)
    list(APPEND OPENSSL_CONFIGURE_OPTIONS
            no-sock
            no-dgram
            --with-rand-seed=none)

    set(OPENSSL_CPPFLAGS_LIST
            -UWIN32
            -U_WIN32
            -DNO_SYSLOG
            -DOPENSSL_NO_SYSLOG
            -Dstrcasecmp=_stricmp
            -Dstrncasecmp=_strnicmp
            -D_stat=stat
            -D_fstat=fstat
            -D_exit=_Exit
            -include
            "${CMAKE_SOURCE_DIR}/src/compat/openssl/openssl_apps_compat.h"
            "-I${CMAKE_SOURCE_DIR}/src/compat/openssl"
            "-I${NXDK_DIR}/lib"
            "-I${NXDK_DIR}/lib/xboxrt/libc_extensions"
            "-I${NXDK_DIR}/lib/pdclib/include"
            "-I${NXDK_DIR}/lib/pdclib/platform/xbox/include"
            "-I${NXDK_DIR}/lib/winapi"
            "-I${NXDK_DIR}/lib/xboxrt/vcruntime"
            "-I${NXDK_DIR}/lib/net/lwip/src/include"
            "-I${NXDK_DIR}/lib/net/nforceif/include"
    )
    list(JOIN OPENSSL_CPPFLAGS_LIST " " OPENSSL_CPPFLAGS)

    list(APPEND OPENSSL_ENV
            "NXDK_DIR=${NXDK_DIR}"
            "CC=${NXDK_DIR}/bin/nxdk-cc"
            "CXX=${NXDK_DIR}/bin/nxdk-cxx"
            "AR=llvm-ar"
            "RANLIB=llvm-ranlib"
            "CPPFLAGS=${OPENSSL_CPPFLAGS}")
else()
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(OPENSSL_CONFIGURE_TARGET mingw64)
    else()
        set(OPENSSL_CONFIGURE_TARGET mingw)
    endif()

    if(NOT DEFINED CMAKE_MAKE_PROGRAM OR CMAKE_MAKE_PROGRAM STREQUAL "")
        message(FATAL_ERROR "CMAKE_MAKE_PROGRAM must be defined for the host vendored OpenSSL build")
    endif()
    set(OPENSSL_MAKE_EXECUTABLE "${CMAKE_MAKE_PROGRAM}")

    list(APPEND OPENSSL_ENV
            "CC=${CMAKE_C_COMPILER}"
            "CXX=${CMAKE_CXX_COMPILER}")

    if(DEFINED CMAKE_AR AND NOT CMAKE_AR STREQUAL "")
        list(APPEND OPENSSL_ENV "AR=${CMAKE_AR}")
    endif()
    if(DEFINED CMAKE_RANLIB AND NOT CMAKE_RANLIB STREQUAL "")
        list(APPEND OPENSSL_ENV "RANLIB=${CMAKE_RANLIB}")
    endif()
endif()

ExternalProject_Add(openssl_external
        SOURCE_DIR "${OPENSSL_SOURCE_DIR}"
        BINARY_DIR "${OPENSSL_BUILD_DIR}"
        CONFIGURE_COMMAND
            ${OPENSSL_ENV}
            "${PERL_EXECUTABLE}" "${OPENSSL_SOURCE_DIR}/Configure"
            ${OPENSSL_CONFIGURE_TARGET}
            ${OPENSSL_CONFIGURE_OPTIONS}
            "--prefix=${OPENSSL_INSTALL_DIR}"
            "--openssldir=${OPENSSL_INSTALL_DIR}/ssl"
        BUILD_COMMAND
            ${OPENSSL_ENV}
            ${OPENSSL_MAKE_EXECUTABLE} build_libs
        INSTALL_COMMAND
            ${OPENSSL_ENV}
            ${OPENSSL_MAKE_EXECUTABLE} install_dev
        BUILD_BYPRODUCTS
            "${OPENSSL_INSTALL_DIR}/lib/libcrypto.a"
            "${OPENSSL_INSTALL_DIR}/lib/libssl.a"
        LOG_CONFIGURE ON
        LOG_BUILD ON
        LOG_INSTALL ON
        LOG_MERGED_STDOUTERR ON
        LOG_OUTPUT_ON_FAILURE ON
        DOWNLOAD_COMMAND ""
        UPDATE_COMMAND ""
        PATCH_COMMAND ""
)

set(OpenSSL_FOUND TRUE)  # cmake-lint: disable=C0103
set(OPENSSL_INCLUDE_DIR "${OPENSSL_INSTALL_DIR}/include")
set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_INSTALL_DIR}/lib/libcrypto.a")
set(OPENSSL_SSL_LIBRARY "${OPENSSL_INSTALL_DIR}/lib/libssl.a")
set(OPENSSL_LIBRARIES OpenSSL::Crypto OpenSSL::SSL)

if(NOT TARGET OpenSSL::Crypto)
    add_library(OpenSSL::Crypto STATIC IMPORTED GLOBAL)
    set_target_properties(OpenSSL::Crypto PROPERTIES
            IMPORTED_LOCATION "${OPENSSL_CRYPTO_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}")
    if(MOONLIGHT_OPENSSL_PLATFORM STREQUAL "HOST" AND (WIN32 OR MINGW OR CMAKE_HOST_WIN32))
        target_link_libraries(OpenSSL::Crypto INTERFACE ws2_32 crypt32 gdi32 advapi32 user32)
    endif()
    add_dependencies(OpenSSL::Crypto openssl_external)
endif()

if(NOT TARGET OpenSSL::SSL)
    add_library(OpenSSL::SSL STATIC IMPORTED GLOBAL)
    set_target_properties(OpenSSL::SSL PROPERTIES
            IMPORTED_LOCATION "${OPENSSL_SSL_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}")
    target_link_libraries(OpenSSL::SSL INTERFACE OpenSSL::Crypto)
    add_dependencies(OpenSSL::SSL openssl_external)
endif()

message(STATUS "OpenSSL version: ${OPENSSL_VERSION}")
message(STATUS "OpenSSL platform: ${MOONLIGHT_OPENSSL_PLATFORM}")
message(STATUS "OpenSSL source dir: ${OPENSSL_SOURCE_DIR}")
message(STATUS "OpenSSL include dir: ${OPENSSL_INCLUDE_DIR}")
