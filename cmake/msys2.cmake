# Detect the Windows MSYS2 installation and expose the paths needed by host and nxdk builds.

include_guard(GLOBAL)

# Normalize a Windows path to cmake-style forward slashes with no trailing slash.
function(_moonlight_normalize_windows_path out_var input_path)
    if("${input_path}" STREQUAL "")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    file(TO_CMAKE_PATH "${input_path}" _normalized_path)
    string(REGEX REPLACE "/$" "" _normalized_path "${_normalized_path}")
    set(${out_var} "${_normalized_path}" PARENT_SCOPE)
endfunction()

# Validate a candidate MSYS2 root path and set the output variable when valid.
function(_moonlight_set_msys2_root_if_valid out_var candidate_root)
    _moonlight_normalize_windows_path(_normalized_root "${candidate_root}")

    if(NOT _normalized_root STREQUAL "" AND EXISTS "${_normalized_root}/msys2_shell.cmd")
        set(${out_var} "${_normalized_root}" PARENT_SCOPE)
    else()
        set(${out_var} "" PARENT_SCOPE)
    endif()
endfunction()

# Attempt to derive the MSYS2 root directory by walking up from a known tool path.
function(_moonlight_try_msys2_root_from_tool out_var tool_path)
    if("${tool_path}" STREQUAL "")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    _moonlight_normalize_windows_path(_tool_path "${tool_path}")
    get_filename_component(_tool_dir "${_tool_path}" DIRECTORY)
    get_filename_component(_tool_name "${_tool_path}" NAME)

    set(candidate_roots)
    if(_tool_name STREQUAL "msys2_shell.cmd")
        list(APPEND candidate_roots "${_tool_dir}")
    endif()

    get_filename_component(_one_up "${_tool_dir}/.." ABSOLUTE)
    get_filename_component(_two_up "${_tool_dir}/../.." ABSOLUTE)
    list(APPEND candidate_roots "${_one_up}" "${_two_up}")

    foreach(candidate_root IN LISTS candidate_roots)
        _moonlight_set_msys2_root_if_valid(_resolved_root "${candidate_root}")
        if(NOT _resolved_root STREQUAL "")
            set(${out_var} "${_resolved_root}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    set(${out_var} "" PARENT_SCOPE)
endfunction()

# Cache one resolved MSYS2 root path.
function(_moonlight_cache_detected_msys2_root resolved_root)
    set(MOONLIGHT_MSYS2_ROOT "${resolved_root}" CACHE PATH "Path to the detected MSYS2 installation" FORCE)
endfunction()

# Build the configured MSYS2 root candidates from cache and environment overrides.
function(_moonlight_get_configured_msys2_root_candidates out_var)
    set(candidate_roots)
    if(DEFINED MOONLIGHT_MSYS2_ROOT AND NOT MOONLIGHT_MSYS2_ROOT STREQUAL "")
        list(APPEND candidate_roots "${MOONLIGHT_MSYS2_ROOT}")
    endif()
    if(DEFINED ENV{MOONLIGHT_MSYS2_ROOT} AND NOT "$ENV{MOONLIGHT_MSYS2_ROOT}" STREQUAL "")
        list(APPEND candidate_roots "$ENV{MOONLIGHT_MSYS2_ROOT}")
    endif()
    if(DEFINED ENV{MSYS2_ROOT} AND NOT "$ENV{MSYS2_ROOT}" STREQUAL "")
        list(APPEND candidate_roots "$ENV{MSYS2_ROOT}")
    endif()

    set(${out_var} "${candidate_roots}" PARENT_SCOPE)
endfunction()

# Build the default MSYS2 root candidates used when no explicit configuration is available.
function(_moonlight_get_default_msys2_root_candidates out_var)
    set(candidate_roots)
    if(DEFINED ENV{SystemDrive} AND NOT "$ENV{SystemDrive}" STREQUAL "")
        list(APPEND candidate_roots "$ENV{SystemDrive}/msys64")
    endif()
    list(APPEND candidate_roots
            "C:/msys64"
            "C:/tools/msys64"
    )

    set(${out_var} "${candidate_roots}" PARENT_SCOPE)
endfunction()

# Try to resolve an MSYS2 root from a list of candidate root directories.
function(_moonlight_try_msys2_root_candidates out_var)
    foreach(candidate_root IN LISTS ARGN)
        _moonlight_set_msys2_root_if_valid(_resolved_root "${candidate_root}")
        if(NOT _resolved_root STREQUAL "")
            set(${out_var} "${_resolved_root}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    set(${out_var} "" PARENT_SCOPE)
endfunction()

# Try to resolve an MSYS2 root by walking up from the given tool paths.
function(_moonlight_try_msys2_root_from_tools out_var)
    foreach(tool_path IN LISTS ARGN)
        _moonlight_try_msys2_root_from_tool(_resolved_root "${tool_path}")
        if(NOT _resolved_root STREQUAL "")
            set(${out_var} "${_resolved_root}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    set(${out_var} "" PARENT_SCOPE)
endfunction()

# Try to resolve an MSYS2 root by searching PATH for common MSYS2 tool names.
function(_moonlight_try_msys2_root_from_path_tools out_var)
    foreach(tool_name
            msys2_shell.cmd
            bash.exe
            make.exe
            mingw32-make.exe
            clang++.exe
            clang.exe
            g++.exe
            gcc.exe
            c++.exe
            cc.exe)
        find_program(_tool_path NAMES ${tool_name})
        _moonlight_try_msys2_root_from_tool(_resolved_root "${_tool_path}")
        if(NOT _resolved_root STREQUAL "")
            set(${out_var} "${_resolved_root}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    set(${out_var} "" PARENT_SCOPE)
endfunction()

# Try to resolve an MSYS2 root by searching under hinted installation roots.
function(_moonlight_try_hinted_msys2_root out_var)
    set(program_hints ${ARGN})

    find_program(_msys2_shell_path
            NAMES msys2_shell.cmd
            HINTS ${program_hints}
            PATH_SUFFIXES .
    )
    _moonlight_try_msys2_root_from_tool(_resolved_root "${_msys2_shell_path}")
    if(NOT _resolved_root STREQUAL "")
        set(${out_var} "${_resolved_root}" PARENT_SCOPE)
        return()
    endif()

    foreach(tool_name bash.exe make.exe mingw32-make.exe clang++.exe clang.exe g++.exe gcc.exe c++.exe cc.exe)
        find_program(_tool_path
                NAMES ${tool_name}
                HINTS ${program_hints}
                PATH_SUFFIXES usr/bin mingw64/bin ucrt64/bin clang64/bin clangarm64/bin mingw32/bin
        )
        _moonlight_try_msys2_root_from_tool(_resolved_root "${_tool_path}")
        if(NOT _resolved_root STREQUAL "")
            set(${out_var} "${_resolved_root}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    set(${out_var} "" PARENT_SCOPE)
endfunction()

# Detect the MSYS2 installation root on Windows and cache the resolved path.
function(moonlight_detect_windows_msys2_root out_var)
    if(NOT WIN32)
        message(FATAL_ERROR "moonlight_detect_windows_msys2_root is only available on Windows hosts")
    endif()

    _moonlight_get_configured_msys2_root_candidates(candidate_roots)
    _moonlight_try_msys2_root_candidates(_resolved_root ${candidate_roots})
    if(NOT _resolved_root STREQUAL "")
        _moonlight_cache_detected_msys2_root("${_resolved_root}")
        set(${out_var} "${_resolved_root}" PARENT_SCOPE)
        return()
    endif()

    _moonlight_try_msys2_root_from_tools(_resolved_root "${CMAKE_COMMAND}" "${CMAKE_MAKE_PROGRAM}")
    if(NOT _resolved_root STREQUAL "")
        _moonlight_cache_detected_msys2_root("${_resolved_root}")
        set(${out_var} "${_resolved_root}" PARENT_SCOPE)
        return()
    endif()

    _moonlight_try_msys2_root_from_path_tools(_resolved_root)
    if(NOT _resolved_root STREQUAL "")
        _moonlight_cache_detected_msys2_root("${_resolved_root}")
        set(${out_var} "${_resolved_root}" PARENT_SCOPE)
        return()
    endif()

    _moonlight_get_default_msys2_root_candidates(default_candidate_roots)
    list(APPEND candidate_roots ${default_candidate_roots})

    _moonlight_try_msys2_root_candidates(_resolved_root ${candidate_roots})
    if(NOT _resolved_root STREQUAL "")
        _moonlight_cache_detected_msys2_root("${_resolved_root}")
        set(${out_var} "${_resolved_root}" PARENT_SCOPE)
        return()
    endif()

    _moonlight_try_hinted_msys2_root(_resolved_root ${candidate_roots})
    if(NOT _resolved_root STREQUAL "")
        _moonlight_cache_detected_msys2_root("${_resolved_root}")
        set(${out_var} "${_resolved_root}" PARENT_SCOPE)
        return()
    endif()

    message(FATAL_ERROR
            "Could not find an MSYS2 installation. "
            "Set the MSYS2_ROOT environment variable or add the MSYS2 tools to PATH.")
endfunction()

# Get the path to the MSYS2 shell script.
function(moonlight_get_windows_msys2_shell out_var)
    moonlight_detect_windows_msys2_root(_msys2_root)
    set(msys2_shell "${_msys2_root}/msys2_shell.cmd")
    set(${out_var} "${msys2_shell}" PARENT_SCOPE)
endfunction()

# Get the MSYS2 usr/bin directory path.
function(moonlight_get_windows_msys2_usr_bin out_var)
    moonlight_detect_windows_msys2_root(_msys2_root)
    set(msys2_usr_bin "${_msys2_root}/usr/bin")
    set(${out_var} "${msys2_usr_bin}" PARENT_SCOPE)
endfunction()

# Get the path to the MSYS2 bash executable, failing if it does not exist.
function(moonlight_get_windows_msys2_bash out_var)
    moonlight_get_windows_msys2_usr_bin(_msys2_usr_bin)
    set(bash_executable "${_msys2_usr_bin}/bash.exe")

    if(NOT EXISTS "${bash_executable}")
        message(FATAL_ERROR "MSYS2 bash not found at ${bash_executable}")
    endif()

    set(${out_var} "${bash_executable}" PARENT_SCOPE)
endfunction()

# Get the bin directory for the specified MSYS2 MSYSTEM environment (default: mingw64).
function(moonlight_get_windows_msys2_msystem_bin out_var)
    set(msystem_dir "mingw64")
    if(ARGC GREATER 1 AND NOT "${ARGV1}" STREQUAL "")
        set(msystem_dir "${ARGV1}")
    endif()

    moonlight_detect_windows_msys2_root(_msys2_root)
    set(msystem_bin "${_msys2_root}/${msystem_dir}/bin")

    if(NOT EXISTS "${msystem_bin}")
        message(FATAL_ERROR "MSYS2 ${msystem_dir} bin directory not found at ${msystem_bin}")
    endif()

    set(${out_var} "${msystem_bin}" PARENT_SCOPE)
endfunction()

# Build a PATH-style string prepending the MSYS2 MSYSTEM and usr/bin directories.
function(moonlight_prepend_windows_msys2_path out_var)
    set(msystem_dir "mingw64")
    if(ARGC GREATER 1 AND NOT "${ARGV1}" STREQUAL "")
        set(msystem_dir "${ARGV1}")
    endif()

    moonlight_get_windows_msys2_msystem_bin(_msystem_bin "${msystem_dir}")
    moonlight_get_windows_msys2_usr_bin(_msys2_usr_bin)

    set(path_entries "${_msystem_bin}" "${_msys2_usr_bin}")
    file(TO_CMAKE_PATH "$ENV{PATH}" _existing_path)
    if(NOT _existing_path STREQUAL "")
        list(APPEND path_entries ${_existing_path})
    endif()

    list(REMOVE_DUPLICATES path_entries)
    list(JOIN path_entries ";" resolved_path)
    set(${out_var} "${resolved_path}" PARENT_SCOPE)
endfunction()
