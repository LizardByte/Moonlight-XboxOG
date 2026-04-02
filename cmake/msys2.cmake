# Detect the Windows MSYS2 installation and expose the paths needed by host and nxdk builds.

include_guard(GLOBAL)

function(_moonlight_normalize_windows_path out_var input_path)
    if("${input_path}" STREQUAL "")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    file(TO_CMAKE_PATH "${input_path}" _normalized_path)
    string(REGEX REPLACE "/$" "" _normalized_path "${_normalized_path}")
    set(${out_var} "${_normalized_path}" PARENT_SCOPE)
endfunction()

function(_moonlight_set_msys2_root_if_valid out_var candidate_root)
    _moonlight_normalize_windows_path(_normalized_root "${candidate_root}")

    if(NOT _normalized_root STREQUAL "" AND EXISTS "${_normalized_root}/msys2_shell.cmd")
        set(${out_var} "${_normalized_root}" PARENT_SCOPE)
    else()
        set(${out_var} "" PARENT_SCOPE)
    endif()
endfunction()

function(_moonlight_try_msys2_root_from_tool out_var tool_path)
    if("${tool_path}" STREQUAL "")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    _moonlight_normalize_windows_path(_tool_path "${tool_path}")
    get_filename_component(_tool_dir "${_tool_path}" DIRECTORY)
    get_filename_component(_tool_name "${_tool_path}" NAME)

    set(_candidate_roots)
    if(_tool_name STREQUAL "msys2_shell.cmd")
        list(APPEND _candidate_roots "${_tool_dir}")
    endif()

    get_filename_component(_one_up "${_tool_dir}/.." ABSOLUTE)
    get_filename_component(_two_up "${_tool_dir}/../.." ABSOLUTE)
    list(APPEND _candidate_roots "${_one_up}" "${_two_up}")

    foreach(_candidate_root IN LISTS _candidate_roots)
        _moonlight_set_msys2_root_if_valid(_resolved_root "${_candidate_root}")
        if(NOT _resolved_root STREQUAL "")
            set(${out_var} "${_resolved_root}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    set(${out_var} "" PARENT_SCOPE)
endfunction()

function(moonlight_detect_windows_msys2_root out_var)
    if(NOT WIN32)
        message(FATAL_ERROR "moonlight_detect_windows_msys2_root is only available on Windows hosts")
    endif()

    set(_candidate_roots)
    if(DEFINED MOONLIGHT_MSYS2_ROOT AND NOT MOONLIGHT_MSYS2_ROOT STREQUAL "")
        list(APPEND _candidate_roots "${MOONLIGHT_MSYS2_ROOT}")
    endif()
    if(DEFINED ENV{MSYS2_ROOT} AND NOT "$ENV{MSYS2_ROOT}" STREQUAL "")
        list(APPEND _candidate_roots "$ENV{MSYS2_ROOT}")
    endif()
    if(DEFINED ENV{SystemDrive} AND NOT "$ENV{SystemDrive}" STREQUAL "")
        list(APPEND _candidate_roots "$ENV{SystemDrive}/msys64")
    endif()
    list(APPEND _candidate_roots
            "C:/msys64"
            "C:/tools/msys64"
    )

    foreach(_candidate_root IN LISTS _candidate_roots)
        _moonlight_set_msys2_root_if_valid(_resolved_root "${_candidate_root}")
        if(NOT _resolved_root STREQUAL "")
            set(MOONLIGHT_MSYS2_ROOT "${_resolved_root}" CACHE PATH "Path to the detected MSYS2 installation" FORCE)
            set(${out_var} "${_resolved_root}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    set(_program_hints ${_candidate_roots})

    find_program(_msys2_shell_path
            NAMES msys2_shell.cmd
            HINTS ${_program_hints}
            PATH_SUFFIXES .
    )
    _moonlight_try_msys2_root_from_tool(_resolved_root "${_msys2_shell_path}")
    if(NOT _resolved_root STREQUAL "")
        set(MOONLIGHT_MSYS2_ROOT "${_resolved_root}" CACHE PATH "Path to the detected MSYS2 installation" FORCE)
        set(${out_var} "${_resolved_root}" PARENT_SCOPE)
        return()
    endif()

    foreach(_tool_name bash.exe mingw32-make.exe clang++.exe clang.exe)
        find_program(_tool_path
                NAMES ${_tool_name}
                HINTS ${_program_hints}
                PATH_SUFFIXES usr/bin mingw64/bin ucrt64/bin clang64/bin clangarm64/bin mingw32/bin
        )
        _moonlight_try_msys2_root_from_tool(_resolved_root "${_tool_path}")
        if(NOT _resolved_root STREQUAL "")
            set(MOONLIGHT_MSYS2_ROOT "${_resolved_root}" CACHE PATH "Path to the detected MSYS2 installation" FORCE)
            set(${out_var} "${_resolved_root}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    message(FATAL_ERROR
            "Could not find an MSYS2 installation. Set the MSYS2_ROOT environment variable or add the MSYS2 tools to PATH.")
endfunction()

function(moonlight_get_windows_msys2_shell out_var)
    moonlight_detect_windows_msys2_root(_msys2_root)
    set(_msys2_shell "${_msys2_root}/msys2_shell.cmd")
    set(${out_var} "${_msys2_shell}" PARENT_SCOPE)
endfunction()

function(moonlight_get_windows_msys2_usr_bin out_var)
    moonlight_detect_windows_msys2_root(_msys2_root)
    set(_msys2_usr_bin "${_msys2_root}/usr/bin")
    set(${out_var} "${_msys2_usr_bin}" PARENT_SCOPE)
endfunction()

function(moonlight_get_windows_msys2_bash out_var)
    moonlight_get_windows_msys2_usr_bin(_msys2_usr_bin)
    set(_bash_executable "${_msys2_usr_bin}/bash.exe")

    if(NOT EXISTS "${_bash_executable}")
        message(FATAL_ERROR "MSYS2 bash not found at ${_bash_executable}")
    endif()

    set(${out_var} "${_bash_executable}" PARENT_SCOPE)
endfunction()

function(moonlight_get_windows_msys2_msystem_bin out_var)
    set(_msystem_dir "mingw64")
    if(ARGC GREATER 1 AND NOT "${ARGV1}" STREQUAL "")
        set(_msystem_dir "${ARGV1}")
    endif()

    moonlight_detect_windows_msys2_root(_msys2_root)
    set(_msystem_bin "${_msys2_root}/${_msystem_dir}/bin")

    if(NOT EXISTS "${_msystem_bin}")
        message(FATAL_ERROR "MSYS2 ${_msystem_dir} bin directory not found at ${_msystem_bin}")
    endif()

    set(${out_var} "${_msystem_bin}" PARENT_SCOPE)
endfunction()

function(moonlight_prepend_windows_msys2_path out_var)
    set(_msystem_dir "mingw64")
    if(ARGC GREATER 1 AND NOT "${ARGV1}" STREQUAL "")
        set(_msystem_dir "${ARGV1}")
    endif()

    moonlight_get_windows_msys2_msystem_bin(_msystem_bin "${_msystem_dir}")
    moonlight_get_windows_msys2_usr_bin(_msys2_usr_bin)

    set(_path_entries "${_msystem_bin}" "${_msys2_usr_bin}")
    file(TO_CMAKE_PATH "$ENV{PATH}" _existing_path)
    if(NOT _existing_path STREQUAL "")
        list(APPEND _path_entries ${_existing_path})
    endif()

    list(REMOVE_DUPLICATES _path_entries)
    list(JOIN _path_entries ";" _resolved_path)
    set(${out_var} "${_resolved_path}" PARENT_SCOPE)
endfunction()
