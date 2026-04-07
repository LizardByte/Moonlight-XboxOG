include_guard(GLOBAL)

macro(moonlight_prepare_common_dependencies)
    include(GetOpenSSL REQUIRED)

    if(NOT TARGET moonlight-openssl)
        add_library(moonlight-openssl INTERFACE)
        add_library(Moonlight::OpenSSL ALIAS moonlight-openssl)
        target_link_libraries(moonlight-openssl
                INTERFACE
                OpenSSL::SSL
                OpenSSL::Crypto)
    endif()

    set(ENET_NO_INSTALL ON CACHE BOOL "Do not install libraries built for enet" FORCE)

    set(_moonlight_restore_build_shared_libs FALSE)
    if(DEFINED BUILD_SHARED_LIBS)
        set(_moonlight_restore_build_shared_libs TRUE)
        set(_moonlight_saved_build_shared_libs "${BUILD_SHARED_LIBS}")
    endif()

    set(BUILD_SHARED_LIBS OFF)

    if(NOT TARGET moonlight-common-c)
        add_subdirectory(
                "${CMAKE_SOURCE_DIR}/third-party/moonlight-common-c"
                "${CMAKE_BINARY_DIR}/third-party/moonlight-common-c"
        )
    endif()

    if(_moonlight_restore_build_shared_libs)
        set(BUILD_SHARED_LIBS "${_moonlight_saved_build_shared_libs}")
    else()
        unset(BUILD_SHARED_LIBS)
    endif()

    if(TARGET moonlight-common-c AND DEFINED MOONLIGHT_OPENSSL_EXTERNAL_TARGET)
        if(TARGET ${MOONLIGHT_OPENSSL_EXTERNAL_TARGET})
            add_dependencies(moonlight-common-c ${MOONLIGHT_OPENSSL_EXTERNAL_TARGET})
        endif()
    endif()

    if(TARGET moonlight-common-c
            AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_compile_options(moonlight-common-c PRIVATE -Wno-error=cast-function-type)
    endif()

    if(MOONLIGHT_BUILD_KIND STREQUAL "XBOX")
        if(NOT DEFINED NXDK_DIR OR NXDK_DIR STREQUAL "")
            message(FATAL_ERROR "NXDK_DIR must be defined before preparing Xbox dependencies")
        endif()

        set(MOONLIGHT_NXDK_NET_INCLUDE_DIR "${NXDK_DIR}/lib/net")
        set(MOONLIGHT_NXDK_LIBC_EXTENSIONS_DIR "${NXDK_DIR}/lib/xboxrt/libc_extensions")
        set(MOONLIGHT_NXDK_LWIP_POSIX_COMPAT_DIR "${NXDK_DIR}/lib/net/lwip/src/include/compat/posix")

        if(TARGET enet)
            target_link_libraries(enet PUBLIC NXDK::NXDK NXDK::Net)
            target_include_directories(enet PRIVATE
                    "${MOONLIGHT_NXDK_NET_INCLUDE_DIR}"
                    "${MOONLIGHT_NXDK_LIBC_EXTENSIONS_DIR}"
                    "${MOONLIGHT_NXDK_LWIP_POSIX_COMPAT_DIR}")
            target_compile_options(enet PRIVATE -Wno-unused-function -Wno-error=unused-function)
        endif()

        if(TARGET moonlight-common-c)
            target_include_directories(moonlight-common-c PRIVATE
                    "${MOONLIGHT_NXDK_NET_INCLUDE_DIR}"
                    "${MOONLIGHT_NXDK_LIBC_EXTENSIONS_DIR}"
                    "${MOONLIGHT_NXDK_LWIP_POSIX_COMPAT_DIR}")
            target_compile_options(moonlight-common-c PRIVATE
                    -Wno-unused-function
                    -Wno-error=unused-function)
        endif()
    endif()
endmacro()
