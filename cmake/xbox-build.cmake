# Build the Xbox child project with the stock nxdk toolchain, then package the
# resulting executable into the XBE and ISO artifacts consumed by xemu and CI.

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/sources.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/nxdk.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/moonlight-dependencies.cmake")

#
# metadata
#
set(XBE_TITLE ${CMAKE_PROJECT_NAME})
set(XBOX_XBE_DIR "${CMAKE_CURRENT_BINARY_DIR}/xbe")
set(XBOX_ISO_NAME "${CMAKE_PROJECT_NAME}.iso")
set(XBOX_ISO "${CMAKE_CURRENT_BINARY_DIR}/${XBOX_ISO_NAME}")

if(NOT EXISTS "${NXDK_DIR}/.moonlight-nxdk-copy")
    message(FATAL_ERROR "Use cmake/nxdk-toolchain.cmake to build with an isolated nxdk copy")
endif()
set(ENV{NXDK_DIR} "${NXDK_DIR}")
moonlight_prepare_nxdk("${NXDK_DIR}" "${NXDK_DIR}/.moonlight-bootstrap")

find_package(NXDK REQUIRED)
include("${CMAKE_SOURCE_DIR}/cmake/nxdk-compat.cmake")
moonlight_prepare_nxdk_compat()

find_package(NXDK_SDL2 REQUIRED)
find_package(NXDK_SDL2_Image REQUIRED)
find_package(NXDK_SDL2_TTF REQUIRED)

# add the automount_d_drive symbol to the linker flags, this is automatic with nxdk when using the Makefile option
# if this is not used, we must add some code to the main function to automount the D drive
# e.g. https://github.com/abaire/nxdk_pgraph_tests/blob/4b7934e6d612a6d17f9ec229a2d72601a5caefc4/src/main.cpp#L118-L122
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -include:_automount_d_drive")

file(MAKE_DIRECTORY "${XBOX_XBE_DIR}")

# ensure assets are copied to the build directory for packaging into ISO
add_custom_target(sync_xbe_assets ALL
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/xbe"
            "${XBOX_XBE_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy
            "${NXDK_DIR}/samples/sdl_ttf/vegur-regular.ttf"
            "${XBOX_XBE_DIR}/assets/fonts/vegur-regular.ttf"
        COMMENT "Sync XBE assets"
)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")
set(CMAKE_C_FLAGS_RELEASE "-O2")

MOONLIGHT_PREPARE_COMMON_DEPENDENCIES()

# Use nanors' scalar backend on the Xbox CPU, which lacks SSSE3 and newer SIMD
# instructions and cannot link the compiler runtime helpers used for x86 dispatch.
set_property(SOURCE
        "${CMAKE_SOURCE_DIR}/third-party/moonlight-common-c/nanors/deps/obl/oblas_lite.c"
        DIRECTORY "${CMAKE_SOURCE_DIR}/third-party/moonlight-common-c"
        APPEND PROPERTY COMPILE_OPTIONS
        -include "${CMAKE_SOURCE_DIR}/src/_nxdk_compat/nanors_compat.h"
)

add_executable(${CMAKE_PROJECT_NAME}
        ${MOONLIGHT_SOURCES}
)
target_sources(${CMAKE_PROJECT_NAME}
        PRIVATE
        "${CMAKE_SOURCE_DIR}/src/_nxdk_compat/fenv_compat.cpp"
        $<TARGET_OBJECTS:moonlight_nxdk_net_init>
)
target_include_directories(${CMAKE_PROJECT_NAME}
        SYSTEM PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${MOONLIGHT_FFMPEG_INCLUDE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/third-party/tomlplusplus/include"
        "${MOONLIGHT_NXDK_NET_INCLUDE_DIR}"
        "${MOONLIGHT_NXDK_LIBC_EXTENSIONS_DIR}"
        "${MOONLIGHT_NXDK_LWIP_POSIX_COMPAT_DIR}"
)
target_link_libraries(${CMAKE_PROJECT_NAME}
        PUBLIC
        moonlight-common-c
        ${MOONLIGHT_FFMPEG_LIBRARIES}
        NXDK::NXDK
        NXDK::NXDK_CXX
        NXDK::Net
        moonlight_nxdk_compat
        NXDK::SDL2
        NXDK::SDL2_Image
        NXDK::SDL2_TTF
        OpenSSL::Crypto
        OpenSSL::SSL
)
target_compile_options(${CMAKE_PROJECT_NAME}
        PRIVATE
        ${MOONLIGHT_COMPILE_OPTIONS}
        $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>
)
target_compile_definitions(${CMAKE_PROJECT_NAME}
        PRIVATE
        XBOX
        NXDK
        TOML_EXCEPTIONS=0
        TOML_ENABLE_WINDOWS_COMPAT=0)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        MOONLIGHT_INPUT_DIAGNOSTICS=$<BOOL:${MOONLIGHT_INPUT_DIAGNOSTICS}>)
add_dependencies(${CMAKE_PROJECT_NAME} moonlight-common-c)

if(BUILD_DOCS)
    include(third-party/dockle/cmake/Dockle.cmake)
    dockle_add_docs(docs TARGETS api)
endif()

# convert the built EXE into the XBE format
add_custom_target(cxbe_convert ALL
        COMMAND "${NXDK_DIR}/tools/cxbe/cxbe"
            -OUT:${XBOX_XBE_DIR}/default.xbe
            -TITLE:${XBE_TITLE}
            "${CMAKE_CURRENT_BINARY_DIR}/${XBE_TITLE}.exe"
        COMMENT "CXBE conversion: EXE -> XBE"
        VERBATIM
)
add_dependencies(cxbe_convert ${CMAKE_PROJECT_NAME})
add_dependencies(cxbe_convert sync_xbe_assets)

# convert the XBE into an ISO
add_custom_target(xbe_iso ALL
        COMMAND "${NXDK_DIR}/tools/extract-xiso/build/extract-xiso"
            -c "${XBOX_XBE_DIR}" "${XBOX_ISO_NAME}"
        COMMENT "CXBE conversion: XBE -> XISO"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        VERBATIM
)
add_dependencies(xbe_iso cxbe_convert)
