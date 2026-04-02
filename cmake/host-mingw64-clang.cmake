# Detect the local MSYS2 installation and configure the host-native Windows build
# to use the mingw64 clang toolchain and MinGW Makefiles.

if(NOT WIN32)
    return()
endif()

include("${CMAKE_CURRENT_LIST_DIR}/msys2.cmake")

moonlight_detect_windows_msys2_root(_msys2_root)
moonlight_get_windows_msys2_msystem_bin(_msys2_mingw_bin mingw64)
moonlight_get_windows_msys2_usr_bin(_msys2_usr_bin)
moonlight_prepend_windows_msys2_path(_msys2_path mingw64)

set(ENV{MSYSTEM} "MINGW64")
set(ENV{PATH} "${_msys2_path}")

find_program(_clang_executable NAMES clang.exe HINTS "${_msys2_mingw_bin}" NO_DEFAULT_PATH REQUIRED)
find_program(_clangxx_executable NAMES clang++.exe HINTS "${_msys2_mingw_bin}" NO_DEFAULT_PATH REQUIRED)
find_program(_mingw_make_executable NAMES mingw32-make.exe HINTS "${_msys2_mingw_bin}" NO_DEFAULT_PATH REQUIRED)

set(MOONLIGHT_HOST_TOOL_DIR "${_msys2_mingw_bin}" CACHE PATH "Path to the detected host-native mingw64 tool directory" FORCE)
set(MOONLIGHT_MSYS2_ROOT "${_msys2_root}" CACHE PATH "Path to the detected MSYS2 installation" FORCE)
set(CMAKE_C_COMPILER "${_clang_executable}" CACHE FILEPATH "Host C compiler for the Windows CLion build" FORCE)
set(CMAKE_CXX_COMPILER "${_clangxx_executable}" CACHE FILEPATH "Host C++ compiler for the Windows CLion build" FORCE)
set(CMAKE_MAKE_PROGRAM "${_mingw_make_executable}" CACHE FILEPATH "Make program for the Windows CLion build" FORCE)
