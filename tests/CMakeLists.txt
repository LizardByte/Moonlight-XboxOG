cmake_minimum_required(VERSION 3.13)
# https://github.com/google/oss-policies-info/blob/main/foundational-cxx-support-matrix.md#foundational-c-support

project(test_moonlight LANGUAGES CXX)

set(GTEST_SOURCE_DIR "${CMAKE_SOURCE_DIR}/third-party/googletest")
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(INSTALL_GMOCK OFF CACHE BOOL "" FORCE)
add_subdirectory("${GTEST_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/googletest" EXCLUDE_FROM_ALL)

set(TEST_COVERAGE_COMPILE_OPTIONS)
set(TEST_COVERAGE_LINK_OPTIONS)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    list(APPEND TEST_COVERAGE_COMPILE_OPTIONS -fprofile-arcs -ftest-coverage -ggdb -O0)
    list(APPEND TEST_COVERAGE_LINK_OPTIONS -fprofile-arcs -ftest-coverage)
endif()

if (WIN32)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)  # cmake-lint: disable=C0103
endif ()

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/tests/*.h
        ${CMAKE_SOURCE_DIR}/tests/*.cpp)

set(MOONLIGHT_TESTABLE_SOURCES
        ${CMAKE_SOURCE_DIR}/src/splash/splash_layout.cpp
        ${CMAKE_SOURCE_DIR}/src/startup/video_mode.cpp)

add_executable(${PROJECT_NAME}
        ${TEST_SOURCES}
        ${MOONLIGHT_TESTABLE_SOURCES})

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)
target_include_directories(${PROJECT_NAME}
        PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${GTEST_SOURCE_DIR}/googletest/include
        ${GTEST_SOURCE_DIR}/googlemock/include)
target_link_libraries(${PROJECT_NAME}
        PRIVATE
        gtest_main)
target_compile_options(${PROJECT_NAME}
        PRIVATE
        ${TEST_COVERAGE_COMPILE_OPTIONS})
target_link_options(${PROJECT_NAME}
        PRIVATE
        ${TEST_COVERAGE_LINK_OPTIONS})

add_dependencies(${PROJECT_NAME} gtest gtest_main gmock gmock_main)

include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME})
