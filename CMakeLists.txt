cmake_minimum_required(VERSION 3.18)
# much of this file is borrowed from https://github.com/Ryzee119/Xenium-Tools/blob/master/CMakeLists.txt
# and https://github.com/abaire/nxdk_pgraph_tests/blob/main/CMakeLists.txt

project(Moonlight C CXX)

#
# metadata
#
set(XBE_TITLE ${CMAKE_PROJECT_NAME})
set(NXDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third-party/nxdk")
set(XBOX_XBE_DIR "${CMAKE_CURRENT_BINARY_DIR}/xbe")
set(XBOX_ISO "${CMAKE_PROJECT_NAME}.iso")

#
# Options
#
option(BUILD_TESTS "Build tests" OFF)

# add custom modules
set(CMAKE_MODULE_PATH
        ${CMAKE_MODULE_PATH}
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules/"
)

set(CMAKE_CXX_STANDARD 17)

find_package(NXDK REQUIRED)
find_package(NXDK_SDL2 REQUIRED)
find_package(NXDK_SDL2_Image REQUIRED)

# add the automount_d_drive symbol to the linker flags, this is automatic with nxdk when using the Makefile option
# if this is not used, we must add some code to the main function to automount the D drive
# e.g. https://github.com/abaire/nxdk_pgraph_tests/blob/4b7934e6d612a6d17f9ec229a2d72601a5caefc4/src/main.cpp#L118-L122
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -include:_automount_d_drive")

# create the xbe directory if it doesn't exist
file(MAKE_DIRECTORY ${XBOX_XBE_DIR})

# copy assets directory to xbe directory
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/xbe/" DESTINATION "${XBOX_XBE_DIR}")

include(FindPkgConfig)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")
set(CMAKE_C_FLAGS_RELEASE "-O2")

# submodules
# moonlight common library
# find_package(OpenSSL REQUIRED)
include(GetOpenSSL REQUIRED)
set(ENET_NO_INSTALL ON CACHE BOOL "Don't install any libraries built for enet")
add_subdirectory("${CMAKE_SOURCE_DIR}/third-party/moonlight-common-c")
#target_include_directories(moonlight-common-c PUBLIC "/mingw32/include")
target_link_libraries(enet PUBLIC NXDK::NXDK NXDK::Net NXDK::ws2_32)

set(MOONLIGHT_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
)
set(MOONLIGHT_EXTERNAL_LIBRARIES
        NXDK::NXDK
        NXDK::NXDK_CXX
        NXDK::SDL2
        NXDK::SDL2_Image
)
set(MOONLIGHT_INCLUDE_DIRS
)
set(MOONLIGHT_COMPILE_OPTIONS "-Wno-builtin-macro-redefined")
set(MOONLIGHT_DEFINITIONS "-DXBOX -DNXDK")

# Stop lots of warning spam
add_compile_options(${MOONLIGHT_COMPILE_OPTIONS})
add_definitions(${MOONLIGHT_DEFINITIONS})

add_executable(${CMAKE_PROJECT_NAME}
        ${MOONLIGHT_SOURCES}
)
include_directories(SYSTEM
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${MOONLIGHT_INCLUDE_DIRS})
target_link_libraries(${CMAKE_PROJECT_NAME} PUBLIC
        ${MOONLIGHT_EXTERNAL_LIBRARIES}
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE "")
add_dependencies(${CMAKE_PROJECT_NAME} moonlight-common-c)

#
# tests
#
if(BUILD_TESTS)
    add_subdirectory(tests EXCLUDE_FROM_ALL)
endif()

# Post-build exe to xbe conversion
add_custom_target(cxbe_convert ALL
        VERBATIM COMMAND "${CMAKE_COMMAND}" -E env
            ./tools/cxbe/cxbe
            -OUT:${XBOX_XBE_DIR}/default.xbe
            -TITLE:${XBE_TITLE}
            ${CMAKE_CURRENT_BINARY_DIR}/${XBE_TITLE}.exe
        WORKING_DIRECTORY ${NXDK_DIR}
        COMMENT "CXBE Conversion: [EXE -> XBE]"
)
add_dependencies(cxbe_convert ${CMAKE_PROJECT_NAME})

# Post-build xbe to xiso conversion
add_custom_target(xbe_iso ALL
        VERBATIM COMMAND "${CMAKE_COMMAND}" -E env
            ${NXDK_DIR}/tools/extract-xiso/build/extract-xiso
            -c ${XBOX_XBE_DIR} ${XBOX_ISO}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "CXBE Conversion: [XBE -> XISO]"
)
add_dependencies(xbe_iso cxbe_convert)

set_target_properties(cxbe_convert PROPERTIES OUTPUT_QUIET ON)
set_target_properties(xbe_iso PROPERTIES OUTPUT_QUIET ON)
