# Resolve the vendored nxdk checkout, bootstrap its prerequisites, and drive the internal Xbox child build.

include_guard(GLOBAL)

include(CMakeParseArguments)
include("${CMAKE_CURRENT_LIST_DIR}/msys2.cmake")

set(MOONLIGHT_NXDK_DIR "${CMAKE_SOURCE_DIR}/third-party/nxdk" CACHE PATH "Path to the vendored nxdk checkout")
cmake_path(ABSOLUTE_PATH MOONLIGHT_NXDK_DIR BASE_DIRECTORY "${CMAKE_SOURCE_DIR}" NORMALIZE)

function(moonlight_resolve_nxdk_dir out_var)
    set(_nxdk_dir "${MOONLIGHT_NXDK_DIR}")
    cmake_path(ABSOLUTE_PATH _nxdk_dir BASE_DIRECTORY "${CMAKE_SOURCE_DIR}" NORMALIZE)

    if(NOT EXISTS "${_nxdk_dir}/bin/activate")
        message(FATAL_ERROR
                "NXDK directory not found: ${_nxdk_dir}\n"
                "Run: git submodule update --init --recursive")
    endif()

    set(${out_var} "${_nxdk_dir}" PARENT_SCOPE)
endfunction()

function(_moonlight_get_windows_msys2_shell out_var)
    moonlight_get_windows_msys2_shell(_msys2_shell)
    set(${out_var} "${_msys2_shell}" PARENT_SCOPE)
endfunction()

function(_moonlight_get_xbox_make_program out_var)
    if(WIN32)
        moonlight_get_windows_msys2_usr_bin(_msys2_usr_bin)
        set(_make_program "${_msys2_usr_bin}/make.exe")
        if(NOT EXISTS "${_make_program}")
            message(FATAL_ERROR "MSYS2 make not found at ${_make_program}")
        endif()
    else()
        find_program(_make_program NAMES make REQUIRED)
    endif()

    set(${out_var} "${_make_program}" PARENT_SCOPE)
endfunction()

function(_moonlight_to_msys_path out_var path)
    file(TO_CMAKE_PATH "${path}" _normalized_path)

    if(_normalized_path MATCHES "^([A-Za-z]):/(.*)$")
        string(TOLOWER "${CMAKE_MATCH_1}" _drive)
        set(_normalized_path "/${_drive}/${CMAKE_MATCH_2}")
    endif()

    set(${out_var} "${_normalized_path}" PARENT_SCOPE)
endfunction()

function(_moonlight_shell_quote out_var value)
    string(REPLACE "'" "'\"'\"'" _escaped_value "${value}")
    set(${out_var} "'${_escaped_value}'" PARENT_SCOPE)
endfunction()

function(_moonlight_join_shell_command out_var)
    set(_quoted_args)

    foreach(_arg IN LISTS ARGN)
        _moonlight_shell_quote(_quoted_arg "${_arg}")
        list(APPEND _quoted_args "${_quoted_arg}")
    endforeach()

    list(JOIN _quoted_args " " _command)
    set(${out_var} "${_command}" PARENT_SCOPE)
endfunction()

function(_moonlight_get_nxdk_path out_var nxdk_dir)
    set(_path_entries "${nxdk_dir}/bin")

    if(APPLE)
        foreach(_llvm_path "/opt/homebrew/opt/llvm/bin" "/usr/local/opt/llvm/bin")
            if(EXISTS "${_llvm_path}/clang")
                list(APPEND _path_entries "${_llvm_path}")
            endif()
        endforeach()
    endif()

    if(WIN32)
        moonlight_get_windows_msys2_msystem_bin(_msys2_mingw_bin mingw64)
        moonlight_get_windows_msys2_usr_bin(_msys2_usr_bin)
        list(APPEND _path_entries "${_msys2_mingw_bin}" "${_msys2_usr_bin}")
        file(TO_CMAKE_PATH "$ENV{PATH}" _existing_path)
        if(NOT _existing_path STREQUAL "")
            list(APPEND _path_entries ${_existing_path})
        endif()
        set(_path_separator ";")
    else()
        if(NOT "$ENV{PATH}" STREQUAL "")
            string(REPLACE ":" ";" _existing_path "$ENV{PATH}")
            list(APPEND _path_entries ${_existing_path})
        endif()
        set(_path_separator ":")
    endif()

    list(JOIN _path_entries "${_path_separator}" _path)
    set(${out_var} "${_path}" PARENT_SCOPE)
endfunction()

function(moonlight_run_nxdk_command description nxdk_dir working_directory)
    if(WIN32)
        _moonlight_get_windows_msys2_shell(_msys2_shell)
        _moonlight_to_msys_path(_msys_nxdk_dir "${nxdk_dir}")
        _moonlight_to_msys_path(_msys_working_directory "${working_directory}")
        _moonlight_join_shell_command(_shell_command ${ARGN})
        _moonlight_shell_quote(_quoted_nxdk_dir "${_msys_nxdk_dir}")
        _moonlight_shell_quote(_quoted_working_directory "${_msys_working_directory}")

        set(_shell_script
                "unset MAKEFLAGS MFLAGS GNUMAKEFLAGS MAKELEVEL; export NXDK_DIR=${_quoted_nxdk_dir}; export PATH=\"$NXDK_DIR/bin:$PATH\"; cd ${_quoted_working_directory}; exec ${_shell_command}")

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

function(_moonlight_run_nxdk_make nxdk_dir description)
    if(WIN32)
        set(_make_program make)
    else()
        _moonlight_get_xbox_make_program(_make_program)
    endif()

    moonlight_run_nxdk_command("${description}" "${nxdk_dir}" "${nxdk_dir}" "${_make_program}" ${ARGN})
endfunction()

function(moonlight_prepare_nxdk nxdk_dir state_dir)
    file(MAKE_DIRECTORY "${state_dir}")

    set(_signature_inputs
            "NXDK_DIR=${nxdk_dir}"
            "HOST_SYSTEM=${CMAKE_HOST_SYSTEM_NAME}"
            "NXDK_TOOLCHAIN=${nxdk_dir}/share/toolchain-nxdk.cmake"
            "NXDK_ENV_MODE=cmake-driver"
    )
    list(JOIN _signature_inputs "\n" _signature_text)
    string(SHA256 _signature "${_signature_text}")

    set(_signature_file "${state_dir}/bootstrap.signature")
    set(_cxbe_path "${nxdk_dir}/tools/cxbe/cxbe")
    set(_extract_xiso_source_dir "${nxdk_dir}/tools/extract-xiso")
    set(_extract_xiso_build_dir "${_extract_xiso_source_dir}/build")
    set(_extract_xiso_path "${_extract_xiso_build_dir}/extract-xiso")
    set(_required_libraries
            "${nxdk_dir}/lib/libnxdk.lib"
            "${nxdk_dir}/lib/libc++.lib"
            "${nxdk_dir}/lib/libSDL2.lib"
            "${nxdk_dir}/lib/libSDL2_image.lib"
    )
    set(_required_tools
            "${_cxbe_path}"
            "${_extract_xiso_path}"
    )

    if(MOONLIGHT_FORCE_NXDK_DISTCLEAN)
        set(_need_distclean TRUE)
    else()
        set(_need_distclean FALSE)
    endif()
    set(_need_prepare_libraries FALSE)
    set(_need_prepare_tools FALSE)

    if(NOT EXISTS "${_signature_file}")
        set(_need_distclean TRUE)
    else()
        file(READ "${_signature_file}" _saved_signature)
        string(STRIP "${_saved_signature}" _saved_signature)
        if(NOT _saved_signature STREQUAL _signature)
            set(_need_distclean TRUE)
        endif()
    endif()

    foreach(_required_output IN LISTS _required_libraries)
        if(NOT EXISTS "${_required_output}")
            set(_need_prepare_libraries TRUE)
            break()
        endif()
    endforeach()

    foreach(_required_output IN LISTS _required_tools)
        if(NOT EXISTS "${_required_output}")
            set(_need_prepare_tools TRUE)
            break()
        endif()
    endforeach()

    if(_need_distclean)
        message(STATUS "Cleaning nxdk build tree at ${nxdk_dir}")
        _moonlight_run_nxdk_make("${nxdk_dir}" "nxdk clean" clean)
        set(_need_prepare_libraries TRUE)
    endif()

    if(_need_prepare_libraries)
        message(STATUS "Preparing nxdk libraries at ${nxdk_dir}")
        _moonlight_run_nxdk_make("${nxdk_dir}" "nxdk bootstrap" NXDK_ONLY=y)
    else()
        message(STATUS "Using existing nxdk library outputs from ${nxdk_dir}")
    endif()

    if(_need_prepare_tools)
        if(NOT EXISTS "${_cxbe_path}")
            message(STATUS "Preparing cxbe at ${nxdk_dir}")
            if(WIN32)
                moonlight_run_nxdk_command("cxbe build" "${nxdk_dir}" "${nxdk_dir}/tools/cxbe" make)
            else()
                _moonlight_get_xbox_make_program(_host_make_program)
                moonlight_run_nxdk_command("cxbe build" "${nxdk_dir}" "${nxdk_dir}/tools/cxbe" "${_host_make_program}")
            endif()
        endif()

        if(NOT EXISTS "${_extract_xiso_path}")
            message(STATUS "Preparing extract-xiso at ${nxdk_dir}")
            file(REMOVE_RECURSE "${_extract_xiso_build_dir}")
            file(MAKE_DIRECTORY "${_extract_xiso_build_dir}")
            moonlight_run_nxdk_command(
                    "extract-xiso configure"
                    "${nxdk_dir}"
                    "${nxdk_dir}"
                    "${CMAKE_COMMAND}"
                    -S "${_extract_xiso_source_dir}"
                    -B "${_extract_xiso_build_dir}"
                    -G "Unix Makefiles"
            )
            moonlight_run_nxdk_command(
                    "extract-xiso build"
                    "${nxdk_dir}"
                    "${nxdk_dir}"
                    "${CMAKE_COMMAND}"
                    --build "${_extract_xiso_build_dir}"
            )
        endif()
    else()
        message(STATUS "Using existing nxdk tool outputs from ${nxdk_dir}")
    endif()

    file(WRITE "${_signature_file}" "${_signature}\n")
endfunction()

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

    _moonlight_get_xbox_make_program(_xbox_make_program)

    set(_toolchain_file "${MOONLIGHT_XBOX_NXDK_DIR}/share/toolchain-nxdk.cmake")
    set(_driver_script "${CMAKE_SOURCE_DIR}/cmake/run-child-build.cmake")
    set(_configure_command
            "${CMAKE_COMMAND}"
            -DMOONLIGHT_COMMAND_MODE:STRING=configure
            -DMOONLIGHT_CMAKE_COMMAND:FILEPATH=${CMAKE_COMMAND}
            -DMOONLIGHT_DESCRIPTION:STRING=Configure Xbox child build
            -DMOONLIGHT_NXDK_DIR:PATH=${MOONLIGHT_XBOX_NXDK_DIR}
            -DMOONLIGHT_SOURCE_DIR:PATH=${CMAKE_SOURCE_DIR}
            -DMOONLIGHT_BINARY_DIR:PATH=${MOONLIGHT_XBOX_BINARY_DIR}
            -DMOONLIGHT_WORKING_DIRECTORY:PATH=${CMAKE_SOURCE_DIR}
            -DMOONLIGHT_MAKE_PROGRAM:FILEPATH=${_xbox_make_program}
            -DMOONLIGHT_TOOLCHAIN_FILE:FILEPATH=${_toolchain_file}
            -DMOONLIGHT_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}
            -P "${_driver_script}"
    )
    set(_build_command
            "${CMAKE_COMMAND}"
            -DMOONLIGHT_COMMAND_MODE:STRING=build
            -DMOONLIGHT_CMAKE_COMMAND:FILEPATH=${CMAKE_COMMAND}
            -DMOONLIGHT_DESCRIPTION:STRING=Build Xbox child target
            -DMOONLIGHT_NXDK_DIR:PATH=${MOONLIGHT_XBOX_NXDK_DIR}
            -DMOONLIGHT_BINARY_DIR:PATH=${MOONLIGHT_XBOX_BINARY_DIR}
            -DMOONLIGHT_WORKING_DIRECTORY:PATH=${MOONLIGHT_XBOX_BINARY_DIR}
            -P "${_driver_script}"
    )

    add_custom_target(moonlight_xbox ALL
            COMMAND ${_configure_command}
            COMMAND ${_build_command}
            BYPRODUCTS
                "${MOONLIGHT_XBOX_BINARY_DIR}/Moonlight.iso"
                "${MOONLIGHT_XBOX_BINARY_DIR}/xbe/default.xbe"
            COMMENT "Configure and build the Xbox target in ${MOONLIGHT_XBOX_BINARY_DIR}"
            USES_TERMINAL
            VERBATIM
    )
endfunction()
