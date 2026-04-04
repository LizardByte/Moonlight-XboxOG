# Configure the host-native top-level build, bootstrap vendored nxdk prerequisites,
# and delegate the Xbox target to an internal child configure that uses the stock nxdk toolchain.

cmake_minimum_required(VERSION 3.18)

# Allow third-party subdirectories that use cmake_minimum_required < 3.5 (removed in CMake 4.x)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile commands for IDE tooling" FORCE)

set(MOONLIGHT_BUILD_KIND "HOST" CACHE STRING "Internal Moonlight build mode")
set_property(CACHE MOONLIGHT_BUILD_KIND PROPERTY STRINGS HOST XBOX)

if(MOONLIGHT_BUILD_KIND STREQUAL "XBOX")
    project(Moonlight C CXX)

    # add custom modules
    set(CMAKE_MODULE_PATH
            ${CMAKE_MODULE_PATH}
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules/"
    )

    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    option(BUILD_DOCS "Build documentation" OFF)
    option(MOONLIGHT_SKIP_NXDK_PREP "Skip nxdk bootstrap during the internal Xbox child configure" OFF)

    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/xbox-build.cmake")
    return()
endif()

project(Moonlight C CXX)

set(CMAKE_MODULE_PATH
        ${CMAKE_MODULE_PATH}
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules/"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_HOST_WIN32 AND CMAKE_GENERATOR MATCHES "Makefiles")
    set(CMAKE_DEPENDS_USE_COMPILER FALSE CACHE BOOL "Use CMake depfile scanning with Windows makefile generators" FORCE)
endif()

#
# Options
#
option(BUILD_DOCS "Build documentation" ON)
option(BUILD_TESTS "Build host-native unit tests" ON)
option(BUILD_XBOX "Build the Xbox target through an internal child configure" ON)
option(MOONLIGHT_FORCE_NXDK_DISTCLEAN "Force a fresh nxdk distclean during configure" OFF)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/sources.cmake")

if(BUILD_DOCS)
    add_subdirectory(third-party/doxyconfig docs)
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BUILD_XBOX)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/nxdk.cmake")

    moonlight_resolve_nxdk_dir(MOONLIGHT_NXDK_DIR)
    moonlight_prepare_nxdk("${MOONLIGHT_NXDK_DIR}" "${CMAKE_BINARY_DIR}/nxdk-bootstrap")
    moonlight_add_xbox_build(
            NXDK_DIR "${MOONLIGHT_NXDK_DIR}"
            BINARY_DIR "${CMAKE_BINARY_DIR}/xbox"
    )
endif()
