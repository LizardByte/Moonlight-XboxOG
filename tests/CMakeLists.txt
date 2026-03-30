cmake_minimum_required(VERSION 3.13)
# https://github.com/google/oss-policies-info/blob/main/foundational-cxx-support-matrix.md#foundational-c-support

project(test_moonlight)

include_directories("${CMAKE_SOURCE_DIR}")
include_directories(SYSTEM ${MOONLIGHT_INCLUDE_DIRS})

# nxdk include dirs, these are set by the toolchain for ALL targets except this one since we don't use the toolchain
include_directories(SYSTEM
        "${NXDK_DIR}/lib/libcxx/include"
        "${NXDK_DIR}/lib"
        "${NXDK_DIR}/lib/xboxrt/libc_extensions"
        "${NXDK_DIR}/lib/pdclib/include"
        "${NXDK_DIR}/lib/pdclib/platform/xbox/include"
        "${NXDK_DIR}/lib/winapi"
        "${NXDK_DIR}/lib/winapi/ws2_32"
        "${NXDK_DIR}/lib/xboxrt/vcruntime"
)

enable_testing()

# Add GoogleTest directory to the project
set(GTEST_SOURCE_DIR "${CMAKE_SOURCE_DIR}/third-party/googletest")
set(INSTALL_GTEST OFF)
set(INSTALL_GMOCK OFF)
add_subdirectory("${GTEST_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/googletest" EXCLUDE_FROM_ALL)
include_directories(SYSTEM "${GTEST_SOURCE_DIR}/googletest/include" "${GTEST_SOURCE_DIR}")

# coverage
# https://gcovr.com/en/stable/guide/compiling.html#compiler-options
set(CMAKE_CXX_FLAGS "-fprofile-arcs -ftest-coverage -ggdb -O0")
set(CMAKE_C_FLAGS "-fprofile-arcs -ftest-coverage -ggdb -O0")

# if windows
if (WIN32)
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)  # cmake-lint: disable=C0103
endif ()

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/tests/*.h
        ${CMAKE_SOURCE_DIR}/tests/*.cpp)

# remove main.cpp from the list of sources
# TODO
# list(REMOVE_ITEM MOONLIGHT_SOURCES ${CMAKE_SOURCE_DIR}/src/main.cpp)

# Stop lots of warning spam
add_compile_options(${MOONLIGHT_COMPILE_OPTIONS})
add_definitions(${MOONLIGHT_DEFINITIONS})

add_executable(${PROJECT_NAME}
        ${TEST_SOURCES}
        ${MOONLIGHT_SOURCES})

foreach(dep ${MOONLIGHT_TARGET_DEPENDENCIES})
    add_dependencies(${PROJECT_NAME} ${dep})  # compile these before Moonlight
endforeach()

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20)
target_link_libraries(${PROJECT_NAME}
        ${MOONLIGHT_EXTERNAL_LIBRARIES}
        gtest
        gtest_main  # if we use this we don't need our own main function
)
target_compile_definitions(${PROJECT_NAME} PUBLIC ${MOONLIGHT_DEFINITIONS} ${TEST_DEFINITIONS})
target_compile_options(${PROJECT_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${MOONLIGHT_COMPILE_OPTIONS}>)
target_link_options(${PROJECT_NAME} PRIVATE)

add_dependencies(${PROJECT_NAME} gtest gtest_main gmock gmock_main)

add_test(NAME ${PROJECT_NAME} COMMAND moonlight_test)
