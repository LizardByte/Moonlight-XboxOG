# Build the host-native googletest target that exercises platform-neutral
# Moonlight code without pulling the Xbox-only entrypoints into the test binary.

cmake_minimum_required(VERSION 3.13)
# https://github.com/google/oss-policies-info/blob/main/foundational-cxx-support-matrix.md#foundational-c-support

project(test_moonlight LANGUAGES CXX)

if(NOT DEFINED MOONLIGHT_HOST_TESTABLE_SOURCES)
    message(FATAL_ERROR "tests/CMakeLists.txt must be configured from the top-level CMake project")
endif()

set(GTEST_SOURCE_DIR "${CMAKE_SOURCE_DIR}/third-party/googletest")
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(INSTALL_GMOCK OFF CACHE BOOL "" FORCE)
add_subdirectory("${GTEST_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/googletest" EXCLUDE_FROM_ALL)

if(NOT TARGET moonlight-common-c OR NOT TARGET Moonlight::OpenSSL)
    message(FATAL_ERROR
            "tests/CMakeLists.txt requires the shared Moonlight dependency targets from the top-level configure")
endif()

set(TEST_COVERAGE_COMPILE_OPTIONS)
set(TEST_COVERAGE_LINK_OPTIONS)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
        OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT WIN32))
    list(APPEND TEST_COVERAGE_COMPILE_OPTIONS -fprofile-arcs -ftest-coverage -ggdb -O0)
    list(APPEND TEST_COVERAGE_LINK_OPTIONS -fprofile-arcs -ftest-coverage)
endif()

if(WIN32 OR MINGW OR CMAKE_HOST_WIN32)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)  # cmake-lint: disable=C0103
endif()

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/tests/*.h
        ${CMAKE_SOURCE_DIR}/tests/*.cpp)

add_executable(${PROJECT_NAME}
        ${TEST_SOURCES}
        ${MOONLIGHT_HOST_TESTABLE_SOURCES})

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)

target_include_directories(${PROJECT_NAME}
        PRIVATE
        ${CMAKE_SOURCE_DIR}/tests/support
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/third-party/tomlplusplus/include
        ${GTEST_SOURCE_DIR}/googletest/include
        ${GTEST_SOURCE_DIR}/googlemock/include)
target_link_libraries(${PROJECT_NAME}
        PRIVATE
        gtest_main
        Moonlight::OpenSSL)
target_compile_definitions(${PROJECT_NAME}
        PRIVATE
        TOML_EXCEPTIONS=0
        TOML_ENABLE_WINDOWS_COMPAT=0)
if(WIN32 OR MINGW OR CMAKE_HOST_WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
endif()
target_compile_options(${PROJECT_NAME}
        PRIVATE
        ${TEST_COVERAGE_COMPILE_OPTIONS})
target_link_options(${PROJECT_NAME}
        PRIVATE
        ${TEST_COVERAGE_LINK_OPTIONS})

add_dependencies(${PROJECT_NAME} gtest gtest_main gmock gmock_main moonlight-common-c)

include(GoogleTest)
set(TEST_RUNTIME_PROPERTIES)
if(WIN32 OR MINGW OR CMAKE_HOST_WIN32)
    set(_test_runtime_path "${MOONLIGHT_HOST_TOOL_DIR}")
    if(_test_runtime_path STREQUAL "")
        get_filename_component(_test_runtime_path "${CMAKE_CXX_COMPILER}" DIRECTORY)
    endif()

    list(APPEND TEST_RUNTIME_PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_test_runtime_path}")
endif()

gtest_discover_tests(${PROJECT_NAME}
        DISCOVERY_MODE PRE_TEST
        PROPERTIES ${TEST_RUNTIME_PROPERTIES})
