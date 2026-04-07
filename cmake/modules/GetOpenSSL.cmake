include_guard(GLOBAL)

include(ExternalProject)
include("${CMAKE_CURRENT_LIST_DIR}/../msys2.cmake")

set(MOONLIGHT_OPENSSL_MODE "BUNDLED" CACHE STRING "How to provide OpenSSL for Moonlight: BUNDLED" FORCE)
set_property(CACHE MOONLIGHT_OPENSSL_MODE PROPERTY STRINGS BUNDLED)

set(OPENSSL_VERSION 1.1.1w)
set(OPENSSL_SOURCE_DIR "${CMAKE_SOURCE_DIR}/third-party/openssl")
set(OPENSSL_BUILD_ROOT "${CMAKE_BINARY_DIR}/third-party/openssl")
set(OPENSSL_BUILD_DIR "${OPENSSL_BUILD_ROOT}/build")
set(OPENSSL_INSTALL_DIR "${OPENSSL_BUILD_ROOT}/install")

set(MOONLIGHT_OPENSSL_MODE "BUNDLED")

if(MOONLIGHT_BUILD_KIND STREQUAL "XBOX")
    set(MOONLIGHT_OPENSSL_PLATFORM "XBOX")
else()
    set(MOONLIGHT_OPENSSL_PLATFORM "HOST")
endif()
string(TOLOWER "${MOONLIGHT_OPENSSL_PLATFORM}" MOONLIGHT_OPENSSL_PLATFORM_LOWER)
set(MOONLIGHT_OPENSSL_EXTERNAL_TARGET "openssl_external_${MOONLIGHT_OPENSSL_PLATFORM_LOWER}")

set(MOONLIGHT_OPENSSL_PROVIDER "BUNDLED")

function(_moonlight_to_msys_path out_var path)
    file(TO_CMAKE_PATH "${path}" _normalized_path)

    if(_normalized_path MATCHES "^([A-Za-z]):/(.*)$")
        string(TOLOWER "${CMAKE_MATCH_1}" _drive)
        set(_normalized_path "/${_drive}/${CMAKE_MATCH_2}")
    endif()

    set(${out_var} "${_normalized_path}" PARENT_SCOPE)
endfunction()

function(_moonlight_shell_quote out_var value)
    string(REPLACE "'" "'\"'\"'" _escaped_value "${value}")
    set(${out_var} "'${_escaped_value}'" PARENT_SCOPE)
endfunction()

function(_moonlight_join_shell_command out_var)
    set(_quoted_args)
    foreach(arg IN LISTS ARGN)
        _moonlight_shell_quote(_quoted_arg "${arg}")
        list(APPEND _quoted_args "${_quoted_arg}")
    endforeach()

    list(JOIN _quoted_args " " _command)
    set(${out_var} "${_command}" PARENT_SCOPE)
endfunction()

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

set(OPENSSL_ENV
        ${CMAKE_COMMAND} -E env
        "MAKEFLAGS="
        "MFLAGS="
        "GNUMAKEFLAGS="
        "MAKELEVEL=")
set(MOONLIGHT_OPENSSL_WINDOWS_HOST FALSE)
if(CMAKE_HOST_WIN32)
    set(MOONLIGHT_OPENSSL_WINDOWS_HOST TRUE)
endif()
set(MOONLIGHT_OPENSSL_IN_ACTIVE_MSYS FALSE)
if(MOONLIGHT_OPENSSL_WINDOWS_HOST
        AND DEFINED ENV{MSYSTEM_PREFIX}
        AND NOT "$ENV{MSYSTEM_PREFIX}" STREQUAL "")
    set(MOONLIGHT_OPENSSL_IN_ACTIVE_MSYS TRUE)
endif()
set(OPENSSL_MAKE_ARGS)
if(MOONLIGHT_OPENSSL_WINDOWS_HOST)
    list(APPEND OPENSSL_MAKE_ARGS -j1)
endif()

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
            "${CMAKE_SOURCE_DIR}/src/_nxdk_compat/openssl_compat.h"
            "-I${NXDK_DIR}/lib"
            "-I${NXDK_DIR}/lib/net"
            "-I${NXDK_DIR}/lib/xboxrt/libc_extensions"
            "-I${NXDK_DIR}/lib/pdclib/include"
            "-I${NXDK_DIR}/lib/pdclib/platform/xbox/include"
            "-I${NXDK_DIR}/lib/winapi"
            "-I${NXDK_DIR}/lib/xboxrt/vcruntime"
            "-I${NXDK_DIR}/lib/net/lwip/src/include"
            "-I${NXDK_DIR}/lib/net/lwip/src/include/compat/posix"
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

    set(OPENSSL_BUILD_COMMAND
            ${OPENSSL_ENV}
            ${OPENSSL_MAKE_EXECUTABLE}
            ${OPENSSL_MAKE_ARGS}
            build_libs)
    set(OPENSSL_INSTALL_COMMAND
            ${OPENSSL_ENV}
            ${OPENSSL_MAKE_EXECUTABLE}
            ${OPENSSL_MAKE_ARGS}
            install_dev)
else()
    if(MOONLIGHT_OPENSSL_WINDOWS_HOST)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(OPENSSL_CONFIGURE_TARGET mingw64)
        else()
            set(OPENSSL_CONFIGURE_TARGET mingw)
        endif()

        moonlight_get_windows_msys2_shell(OPENSSL_MSYS2_SHELL)

        _moonlight_to_msys_path(_openssl_source_dir_msys "${OPENSSL_SOURCE_DIR}")
        _moonlight_to_msys_path(_openssl_build_dir_msys "${OPENSSL_BUILD_DIR}")
        _moonlight_to_msys_path(_openssl_install_dir_msys "${OPENSSL_INSTALL_DIR}")
        _moonlight_to_msys_path(_perl_executable_msys "${PERL_EXECUTABLE}")
        get_filename_component(_openssl_c_compiler_name "${CMAKE_C_COMPILER}" NAME)
        get_filename_component(_openssl_cxx_compiler_name "${CMAKE_CXX_COMPILER}" NAME)
        set(_openssl_tool_assignments
                "MAKEFLAGS="
                "MFLAGS="
                "GNUMAKEFLAGS="
                "MAKELEVEL="
                "CC=${_openssl_c_compiler_name}"
                "CXX=${_openssl_cxx_compiler_name}"
                "CFLAGS=-DNOCRYPT"
                "CPPFLAGS=-DWIN32_LEAN_AND_MEAN")
        if(DEFINED CMAKE_AR AND NOT CMAKE_AR STREQUAL "")
            get_filename_component(_openssl_ar_name "${CMAKE_AR}" NAME)
            list(APPEND _openssl_tool_assignments "AR=${_openssl_ar_name}")
        endif()
        if(DEFINED CMAKE_RANLIB AND NOT CMAKE_RANLIB STREQUAL "")
            get_filename_component(_openssl_ranlib_name "${CMAKE_RANLIB}" NAME)
            list(APPEND _openssl_tool_assignments "RANLIB=${_openssl_ranlib_name}")
        endif()
        list(JOIN _openssl_tool_assignments " " _openssl_tool_prefix)
        _moonlight_join_shell_command(_openssl_configure_command
                "${_perl_executable_msys}"
                "${_openssl_source_dir_msys}/Configure"
                "${OPENSSL_CONFIGURE_TARGET}"
                ${OPENSSL_CONFIGURE_OPTIONS}
                "--prefix=${_openssl_install_dir_msys}"
                "--openssldir=${_openssl_install_dir_msys}/ssl")
        _moonlight_shell_quote(_openssl_build_dir_msys_quoted "${_openssl_build_dir_msys}")
        set(OPENSSL_CONFIGURE_COMMAND
                "${OPENSSL_MSYS2_SHELL}"
                -defterm -here -no-start -mingw64
                -c "cd ${_openssl_build_dir_msys_quoted} && ${_openssl_tool_prefix} exec ${_openssl_configure_command}")
        set(OPENSSL_BUILD_COMMAND
                "${OPENSSL_MSYS2_SHELL}"
                -defterm -here -no-start -mingw64
                -c "cd ${_openssl_build_dir_msys_quoted} && ${_openssl_tool_prefix} exec make -j1 build_libs")
        set(OPENSSL_INSTALL_COMMAND
                "${OPENSSL_MSYS2_SHELL}"
                -defterm -here -no-start -mingw64
                -c "cd ${_openssl_build_dir_msys_quoted} && ${_openssl_tool_prefix} exec make -j1 install_dev")
    elseif(APPLE)
        find_program(OPENSSL_MAKE_EXECUTABLE NAMES gmake make REQUIRED)

        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
            set(OPENSSL_CONFIGURE_TARGET darwin64-arm64-cc)
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
            set(OPENSSL_CONFIGURE_TARGET darwin64-x86_64-cc)
        else()
            message(FATAL_ERROR
                    "Unsupported macOS processor '${CMAKE_SYSTEM_PROCESSOR}' for bundled OpenSSL. "
                    "Use MOONLIGHT_OPENSSL_MODE=SYSTEM or add a Configure target mapping.")
        endif()
    elseif(UNIX)
        find_program(OPENSSL_MAKE_EXECUTABLE NAMES gmake make REQUIRED)

        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(OPENSSL_CONFIGURE_TARGET linux-generic64)
        else()
            set(OPENSSL_CONFIGURE_TARGET linux-generic32)
        endif()
    else()
        message(FATAL_ERROR
                "Unsupported host platform for bundled OpenSSL. "
                "Use MOONLIGHT_OPENSSL_MODE=SYSTEM or add a Configure target mapping.")
    endif()

    if(NOT MOONLIGHT_OPENSSL_WINDOWS_HOST)
        list(APPEND OPENSSL_ENV
                "CC=${CMAKE_C_COMPILER}"
                "CXX=${CMAKE_CXX_COMPILER}")

        if(DEFINED CMAKE_AR AND NOT CMAKE_AR STREQUAL "")
            list(APPEND OPENSSL_ENV "AR=${CMAKE_AR}")
        endif()
        if(DEFINED CMAKE_RANLIB AND NOT CMAKE_RANLIB STREQUAL "")
            list(APPEND OPENSSL_ENV "RANLIB=${CMAKE_RANLIB}")
        endif()

        set(OPENSSL_BUILD_COMMAND
                ${OPENSSL_ENV}
                ${OPENSSL_MAKE_EXECUTABLE}
                ${OPENSSL_MAKE_ARGS}
                build_libs)
        set(OPENSSL_INSTALL_COMMAND
                ${OPENSSL_ENV}
                ${OPENSSL_MAKE_EXECUTABLE}
                ${OPENSSL_MAKE_ARGS}
                install_dev)
    endif()
endif()

if(NOT DEFINED OPENSSL_CONFIGURE_COMMAND)
    set(OPENSSL_CONFIGURE_COMMAND
            ${OPENSSL_ENV}
            "${PERL_EXECUTABLE}" "${OPENSSL_SOURCE_DIR}/Configure"
            ${OPENSSL_CONFIGURE_TARGET}
            ${OPENSSL_CONFIGURE_OPTIONS}
            "--prefix=${OPENSSL_INSTALL_DIR}"
            "--openssldir=${OPENSSL_INSTALL_DIR}/ssl")
endif()

ExternalProject_Add(${MOONLIGHT_OPENSSL_EXTERNAL_TARGET}
        SOURCE_DIR "${OPENSSL_SOURCE_DIR}"
        BINARY_DIR "${OPENSSL_BUILD_DIR}"
        CONFIGURE_COMMAND
            ${OPENSSL_CONFIGURE_COMMAND}
        BUILD_COMMAND
            ${OPENSSL_BUILD_COMMAND}
        INSTALL_COMMAND
            ${OPENSSL_INSTALL_COMMAND}
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
    if(WIN32 AND MOONLIGHT_OPENSSL_PLATFORM STREQUAL "HOST")
        target_link_libraries(OpenSSL::Crypto INTERFACE ws2_32)
    endif()
    add_dependencies(OpenSSL::Crypto ${MOONLIGHT_OPENSSL_EXTERNAL_TARGET})
endif()

if(NOT TARGET OpenSSL::SSL)
    add_library(OpenSSL::SSL STATIC IMPORTED GLOBAL)
    set_target_properties(OpenSSL::SSL PROPERTIES
            IMPORTED_LOCATION "${OPENSSL_SSL_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}")
    target_link_libraries(OpenSSL::SSL INTERFACE OpenSSL::Crypto)
    add_dependencies(OpenSSL::SSL ${MOONLIGHT_OPENSSL_EXTERNAL_TARGET})
endif()

message(STATUS "OpenSSL version: ${OPENSSL_VERSION}")
message(STATUS "OpenSSL platform: ${MOONLIGHT_OPENSSL_PLATFORM}")
message(STATUS "OpenSSL external target: ${MOONLIGHT_OPENSSL_EXTERNAL_TARGET}")
message(STATUS "OpenSSL provider: ${MOONLIGHT_OPENSSL_PROVIDER}")
message(STATUS "OpenSSL source dir: ${OPENSSL_SOURCE_DIR}")
message(STATUS "OpenSSL include dir: ${OPENSSL_INCLUDE_DIR}")
