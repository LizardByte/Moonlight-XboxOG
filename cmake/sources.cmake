# Define the shared Moonlight source lists used by both the host-native test build
# and the Xbox child build, including the sources that must stay out of host tests.

include_guard(GLOBAL)

set(MOONLIGHT_SOURCE_ROOT "${CMAKE_SOURCE_DIR}")

file(GLOB_RECURSE MOONLIGHT_SOURCES CONFIGURE_DEPENDS
        "${MOONLIGHT_SOURCE_ROOT}/src/*.cpp"
)

list(REMOVE_ITEM MOONLIGHT_SOURCES
        "${MOONLIGHT_SOURCE_ROOT}/src/_nxdk_compat/poll_compat.cpp"
        "${MOONLIGHT_SOURCE_ROOT}/src/_nxdk_compat/stat_compat.cpp")

set(MOONLIGHT_TEST_EXCLUDED_SOURCES
        "${MOONLIGHT_SOURCE_ROOT}/src/main.cpp"
        "${MOONLIGHT_SOURCE_ROOT}/src/splash/splash_screen.cpp"
        "${MOONLIGHT_SOURCE_ROOT}/src/streaming/ffmpeg_stream_backend.cpp"
        "${MOONLIGHT_SOURCE_ROOT}/src/startup/memory_stats.cpp"
        "${MOONLIGHT_SOURCE_ROOT}/src/streaming/session.cpp"
        "${MOONLIGHT_SOURCE_ROOT}/src/ui/shell_screen.cpp"
)

set(MOONLIGHT_HOST_TESTABLE_SOURCES ${MOONLIGHT_SOURCES})
list(REMOVE_ITEM MOONLIGHT_HOST_TESTABLE_SOURCES ${MOONLIGHT_TEST_EXCLUDED_SOURCES})

set(MOONLIGHT_COMPILE_OPTIONS -Wno-builtin-macro-redefined)
