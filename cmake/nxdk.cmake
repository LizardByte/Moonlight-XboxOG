# Resolve the vendored nxdk checkout, bootstrap its prerequisites, and drive the internal Xbox child build.

include_guard(GLOBAL)

include(CMakeParseArguments)
include("${CMAKE_CURRENT_LIST_DIR}/msys2.cmake")

set(MOONLIGHT_NXDK_DIR "${CMAKE_SOURCE_DIR}/third-party/nxdk" CACHE PATH "Path to the vendored nxdk checkout")
cmake_path(ABSOLUTE_PATH MOONLIGHT_NXDK_DIR BASE_DIRECTORY "${CMAKE_SOURCE_DIR}" NORMALIZE)

# Resolve and validate the nxdk directory, returning its absolute path.
function(moonlight_resolve_nxdk_dir out_var)
    set(nxdk_dir "${MOONLIGHT_NXDK_DIR}")
    cmake_path(ABSOLUTE_PATH nxdk_dir BASE_DIRECTORY "${CMAKE_SOURCE_DIR}" NORMALIZE)

    if(NOT EXISTS "${nxdk_dir}/bin/activate")
        message(FATAL_ERROR
                "NXDK directory not found: ${nxdk_dir}\n"
                "Run: git submodule update --init --recursive")
    endif()

    set(${out_var} "${nxdk_dir}" PARENT_SCOPE)
endfunction()

# Get the path to the MSYS2 shell script for use in Xbox build commands.
function(_moonlight_get_windows_msys2_shell out_var)
    moonlight_get_windows_msys2_shell(_msys2_shell)
    set(${out_var} "${_msys2_shell}" PARENT_SCOPE)
endfunction()

# Locate the make program appropriate for driving the Xbox nxdk build.
function(_moonlight_get_xbox_make_program out_var)
    if(WIN32)
        moonlight_get_windows_msys2_usr_bin(_msys2_usr_bin)
        set(make_program "${_msys2_usr_bin}/make.exe")
        if(NOT EXISTS "${make_program}")
            message(FATAL_ERROR "MSYS2 make not found at ${make_program}")
        endif()
    else()
        find_program(make_program NAMES make REQUIRED)
    endif()

    set(${out_var} "${make_program}" PARENT_SCOPE)
endfunction()

# Convert a native Windows path to an MSYS2 POSIX-style path.
function(_moonlight_to_msys_path out_var path)
    file(TO_CMAKE_PATH "${path}" normalized_path)

    if(normalized_path MATCHES "^([A-Za-z]):/(.*)$")
        string(TOLOWER "${CMAKE_MATCH_1}" _drive)
        set(normalized_path "/${_drive}/${CMAKE_MATCH_2}")
    endif()

    set(${out_var} "${normalized_path}" PARENT_SCOPE)
endfunction()

# Single-quote a value for safe embedding in a POSIX shell command string.
function(_moonlight_shell_quote out_var value)
    string(REPLACE "'" "'\"'\"'" _escaped_value "${value}")
    set(${out_var} "'${_escaped_value}'" PARENT_SCOPE)
endfunction()

# Join a list of arguments into a single-quoted shell command string.
function(_moonlight_join_shell_command out_var)
    set(quoted_args)

    foreach(arg IN LISTS ARGN)
        _moonlight_shell_quote(_quoted_arg "${arg}")
        list(APPEND quoted_args "${_quoted_arg}")
    endforeach()

    list(JOIN quoted_args " " command)
    set(${out_var} "${command}" PARENT_SCOPE)
endfunction()

# Build the PATH string needed to run nxdk build commands on the current platform.
function(_moonlight_get_nxdk_path out_var nxdk_dir)
    set(path_entries "${nxdk_dir}/bin")

    if(APPLE)
        foreach(llvm_path "/opt/homebrew/opt/llvm/bin" "/usr/local/opt/llvm/bin")
            if(EXISTS "${llvm_path}/clang")
                list(APPEND path_entries "${llvm_path}")
            endif()
        endforeach()
    endif()

    if(WIN32)
        moonlight_get_windows_msys2_msystem_bin(_msys2_mingw_bin mingw64)
        moonlight_get_windows_msys2_usr_bin(_msys2_usr_bin)
        list(APPEND path_entries "${_msys2_mingw_bin}" "${_msys2_usr_bin}")
        file(TO_CMAKE_PATH "$ENV{PATH}" _existing_path)
        if(NOT _existing_path STREQUAL "")
            list(APPEND path_entries ${_existing_path})
        endif()
        set(path_separator ";")
    else()
        if(NOT "$ENV{PATH}" STREQUAL "")
            string(REPLACE ":" ";" _existing_path "$ENV{PATH}")
            list(APPEND path_entries ${_existing_path})
        endif()
        set(path_separator ":")
    endif()

    list(JOIN path_entries "${path_separator}" nxdk_path)
    set(${out_var} "${nxdk_path}" PARENT_SCOPE)
endfunction()

# Execute a command inside the nxdk environment, sourcing the NXDK activation on all platforms.
function(moonlight_run_nxdk_command description nxdk_dir working_directory)
    if(WIN32)
        _moonlight_get_windows_msys2_shell(_msys2_shell)
        _moonlight_to_msys_path(_msys_nxdk_dir "${nxdk_dir}")
        _moonlight_to_msys_path(_msys_working_directory "${working_directory}")
        _moonlight_join_shell_command(_shell_command ${ARGN})
        _moonlight_shell_quote(_quoted_nxdk_dir "${_msys_nxdk_dir}")
        _moonlight_shell_quote(_quoted_working_directory "${_msys_working_directory}")

        string(CONCAT _shell_script
                "unset MAKEFLAGS MFLAGS GNUMAKEFLAGS MAKELEVEL; "
                "export NXDK_DIR=${_quoted_nxdk_dir}; "
                "export PATH=\"$NXDK_DIR/bin:$PATH\"; "
                "cd ${_quoted_working_directory}; "
                "exec ${_shell_command}"
        )

        execute_process(
                COMMAND "${_msys2_shell}" -defterm -here -no-start -mingw64 -c "${_shell_script}"
                RESULT_VARIABLE _moonlight_command_result
        )
    else()
        _moonlight_get_nxdk_path(_moonlight_nxdk_path "${nxdk_dir}")

        execute_process(
                COMMAND "${CMAKE_COMMAND}" -E env
                        "NXDK_DIR=${nxdk_dir}"
                        "PATH=${_moonlight_nxdk_path}"
                        ${ARGN}
                WORKING_DIRECTORY "${working_directory}"
                RESULT_VARIABLE _moonlight_command_result
        )
    endif()

    if(NOT _moonlight_command_result EQUAL 0)
        message(FATAL_ERROR "${description} failed with exit code ${_moonlight_command_result}")
    endif()
endfunction()

# Run make inside the nxdk environment with the platform-appropriate make tool.
function(_moonlight_run_nxdk_make nxdk_dir description)
    if(WIN32)
        set(make_program make)
    else()
        _moonlight_get_xbox_make_program(make_program)
    endif()

    moonlight_run_nxdk_command("${description}" "${nxdk_dir}" "${nxdk_dir}" "${make_program}" ${ARGN})
endfunction()

# Report whether any listed output path is missing.
function(_moonlight_has_missing_output out_var)
    set(missing_output FALSE)

    foreach(required_output IN LISTS ARGN)
        if(NOT EXISTS "${required_output}")
            set(missing_output TRUE)
            break()
        endif()
    endforeach()

    set(${out_var} "${missing_output}" PARENT_SCOPE)
endfunction()

# Build the vendored cxbe tool when it is missing.
function(_moonlight_prepare_cxbe nxdk_dir cxbe_path)
    if(EXISTS "${cxbe_path}")
        return()
    endif()

    message(STATUS "Preparing cxbe at ${nxdk_dir}")
    if(WIN32)
        moonlight_run_nxdk_command("cxbe build" "${nxdk_dir}" "${nxdk_dir}/tools/cxbe" make)
        return()
    endif()

    _moonlight_get_xbox_make_program(host_make_program)
    moonlight_run_nxdk_command("cxbe build" "${nxdk_dir}" "${nxdk_dir}/tools/cxbe" "${host_make_program}")
endfunction()

# Configure and build the vendored extract-xiso tool when it is missing.
function(_moonlight_prepare_extract_xiso nxdk_dir source_dir build_dir output_path)
    if(EXISTS "${output_path}")
        return()
    endif()

    message(STATUS "Preparing extract-xiso at ${nxdk_dir}")
    file(REMOVE_RECURSE "${build_dir}")
    file(MAKE_DIRECTORY "${build_dir}")
    moonlight_run_nxdk_command(
            "extract-xiso configure"
            "${nxdk_dir}"
            "${nxdk_dir}"
            "${CMAKE_COMMAND}"
            -S "${source_dir}"
            -B "${build_dir}"
            -G "Unix Makefiles"
    )
    moonlight_run_nxdk_command(
            "extract-xiso build"
            "${nxdk_dir}"
            "${nxdk_dir}"
            "${CMAKE_COMMAND}"
            --build "${build_dir}"
    )
endfunction()

# Build the vendored host-side nxdk tools required by the Moonlight Xbox build.
function(_moonlight_prepare_nxdk_tools nxdk_dir cxbe_path source_dir build_dir output_path)
    _moonlight_prepare_cxbe("${nxdk_dir}" "${cxbe_path}")
    _moonlight_prepare_extract_xiso(
            "${nxdk_dir}"
            "${source_dir}"
            "${build_dir}"
            "${output_path}"
    )
endfunction()

# Bootstrap the vendored nxdk outputs required by the host-first Moonlight build.
function(moonlight_prepare_nxdk nxdk_dir state_dir)
    file(MAKE_DIRECTORY "${state_dir}")

    set(signature_inputs
            "NXDK_DIR=${nxdk_dir}"
            "HOST_SYSTEM=${CMAKE_HOST_SYSTEM_NAME}"
            "NXDK_TOOLCHAIN=${nxdk_dir}/share/toolchain-nxdk.cmake"
            "NXDK_ENV_MODE=cmake-driver"
    )
    list(JOIN signature_inputs "\n" signature_text)
    string(SHA256 signature "${signature_text}")

    set(signature_file "${state_dir}/bootstrap.signature")
    set(cxbe_path "${nxdk_dir}/tools/cxbe/cxbe")
    set(extract_xiso_source_dir "${nxdk_dir}/tools/extract-xiso")
    set(extract_xiso_build_dir "${extract_xiso_source_dir}/build")
    set(extract_xiso_path "${extract_xiso_build_dir}/extract-xiso")
    set(required_libraries
            "${nxdk_dir}/lib/libnxdk.lib"
            "${nxdk_dir}/lib/libc++.lib"
            "${nxdk_dir}/lib/libSDL2.lib"
            "${nxdk_dir}/lib/libSDL2_image.lib"
    )
    set(required_tools
            "${cxbe_path}"
            "${extract_xiso_path}"
    )

    if(MOONLIGHT_FORCE_NXDK_DISTCLEAN)
        set(need_distclean TRUE)
    else()
        set(need_distclean FALSE)
    endif()

    if(NOT EXISTS "${signature_file}")
        set(need_distclean TRUE)
    else()
        file(READ "${signature_file}" saved_signature)
        string(STRIP "${saved_signature}" saved_signature)
        if(NOT saved_signature STREQUAL signature)
            set(need_distclean TRUE)
        endif()
    endif()

    _moonlight_has_missing_output(need_prepare_libraries ${required_libraries})
    _moonlight_has_missing_output(need_prepare_tools ${required_tools})

    if(need_distclean)
        message(STATUS "Cleaning nxdk build tree at ${nxdk_dir}")
        _moonlight_run_nxdk_make("${nxdk_dir}" "nxdk clean" clean)
        set(need_prepare_libraries TRUE)
    endif()

    if(need_prepare_libraries)
        message(STATUS "Preparing nxdk libraries at ${nxdk_dir}")
        _moonlight_run_nxdk_make("${nxdk_dir}" "nxdk bootstrap" NXDK_ONLY=y)
    else()
        message(STATUS "Using existing nxdk library outputs from ${nxdk_dir}")
    endif()

    if(need_prepare_tools)
        _moonlight_prepare_nxdk_tools(
                "${nxdk_dir}"
                "${cxbe_path}"
                "${extract_xiso_source_dir}"
                "${extract_xiso_build_dir}"
                "${extract_xiso_path}"
        )
    else()
        message(STATUS "Using existing nxdk tool outputs from ${nxdk_dir}")
    endif()

    file(WRITE "${signature_file}" "${signature}\n")
endfunction()

# Configure the internal Xbox child build and expose it as the moonlight_xbox target.
function(moonlight_add_xbox_build)
    set(options)
    set(oneValueArgs BINARY_DIR NXDK_DIR)
    cmake_parse_arguments(MOONLIGHT_XBOX "${options}" "${oneValueArgs}" "" ${ARGN})

    if(NOT MOONLIGHT_XBOX_BINARY_DIR)
        message(FATAL_ERROR "moonlight_add_xbox_build requires BINARY_DIR")
    endif()

    if(NOT MOONLIGHT_XBOX_NXDK_DIR)
        message(FATAL_ERROR "moonlight_add_xbox_build requires NXDK_DIR")
    endif()

    _moonlight_get_xbox_make_program(xbox_make_program)

    set(toolchain_file "${MOONLIGHT_XBOX_NXDK_DIR}/share/toolchain-nxdk.cmake")
    set(driver_script "${CMAKE_SOURCE_DIR}/cmake/run-child-build.cmake")
    set(configure_command
            "${CMAKE_COMMAND}"
            -DMOONLIGHT_COMMAND_MODE:STRING=configure
            -DMOONLIGHT_CMAKE_COMMAND:FILEPATH=${CMAKE_COMMAND}
            -DMOONLIGHT_DESCRIPTION:STRING=Configure Xbox child build
            -DMOONLIGHT_NXDK_DIR:PATH=${MOONLIGHT_XBOX_NXDK_DIR}
            -DMOONLIGHT_SOURCE_DIR:PATH=${CMAKE_SOURCE_DIR}
            -DMOONLIGHT_BINARY_DIR:PATH=${MOONLIGHT_XBOX_BINARY_DIR}
            -DMOONLIGHT_WORKING_DIRECTORY:PATH=${CMAKE_SOURCE_DIR}
            -DMOONLIGHT_MAKE_PROGRAM:FILEPATH=${xbox_make_program}
            -DMOONLIGHT_TOOLCHAIN_FILE:FILEPATH=${toolchain_file}
            -DMOONLIGHT_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
            -P "${driver_script}"
    )
    set(build_command
            "${CMAKE_COMMAND}"
            -DMOONLIGHT_COMMAND_MODE:STRING=build
            -DMOONLIGHT_CMAKE_COMMAND:FILEPATH=${CMAKE_COMMAND}
            -DMOONLIGHT_DESCRIPTION:STRING=Build Xbox child target
            -DMOONLIGHT_NXDK_DIR:PATH=${MOONLIGHT_XBOX_NXDK_DIR}
            -DMOONLIGHT_BINARY_DIR:PATH=${MOONLIGHT_XBOX_BINARY_DIR}
            -DMOONLIGHT_WORKING_DIRECTORY:PATH=${MOONLIGHT_XBOX_BINARY_DIR}
            -P "${driver_script}"
    )

    add_custom_target(moonlight_xbox ALL
            COMMAND ${configure_command}
            COMMAND ${build_command}
            BYPRODUCTS
                "${MOONLIGHT_XBOX_BINARY_DIR}/Moonlight.iso"
                "${MOONLIGHT_XBOX_BINARY_DIR}/xbe/default.xbe"
            COMMENT "Configure and build the Xbox target in ${MOONLIGHT_XBOX_BINARY_DIR}"
            USES_TERMINAL
            VERBATIM
    )
endfunction()
