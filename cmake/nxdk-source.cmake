# Keep all nxdk-generated files in an owned build-directory copy.
include_guard(GLOBAL)

# Fingerprint the SDK revision, nested checkouts and local tracked edits.
function(_moonlight_nxdk_source_state source out_signature out_files)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source}" rev-parse HEAD
            OUTPUT_VARIABLE revision OUTPUT_STRIP_TRAILING_WHITESPACE
            COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source}" submodule status --recursive
            OUTPUT_VARIABLE submodules COMMAND_ERROR_IS_FATAL ANY)
    if(submodules MATCHES "(^|\n)-")
        message(FATAL_ERROR "Initialize nxdk's submodules with git submodule update --init --recursive")
    endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source}" status
                --porcelain --untracked-files=no --ignore-submodules=untracked
            OUTPUT_VARIABLE dirty COMMAND_ERROR_IS_FATAL ANY)
    set(inputs "${source}\n${revision}\n${submodules}")
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source}"
                -c core.quotepath=false ls-files --recurse-submodules
            OUTPUT_VARIABLE files OUTPUT_STRIP_TRAILING_WHITESPACE
            COMMAND_ERROR_IS_FATAL ANY)
    string(REPLACE "\n" ";" files "${files}")
    # Preserve local edits to tracked SDK files as well as committed revisions.
    if(NOT dirty STREQUAL "")
        foreach(file IN LISTS files)
            if(IS_SYMLINK "${source}/${file}")
                file(READ_SYMLINK "${source}/${file}" link)
                string(SHA256 hash "symlink:${link}")
            elseif(EXISTS "${source}/${file}")
                file(SHA256 "${source}/${file}" hash)
            else()
                set(hash deleted)
            endif()
            string(APPEND inputs "${file}=${hash}\n")
        endforeach()
    endif()
    string(SHA256 signature "${inputs}")
    set(${out_signature} "${signature}" PARENT_SCOPE)
    set(${out_files} "${files}" PARENT_SCOPE)
endfunction()

# Reuse an unchanged SDK copy or replace its owned build directory with tracked sources.
function(moonlight_stage_nxdk source destination)
    find_package(Git REQUIRED)
    get_filename_component(source "${source}" REALPATH)
    cmake_path(ABSOLUTE_PATH destination NORMALIZE)
    cmake_path(IS_PREFIX source "${destination}" NORMALIZE inside_source)
    if(inside_source)
        message(FATAL_ERROR "The nxdk build copy must be outside its source checkout")
    endif()

    # Toolchain files can be loaded more than once during a single configure.
    get_property(staged GLOBAL PROPERTY MOONLIGHT_STAGED_NXDK)
    if(staged STREQUAL "${source}|${destination}")
        return()
    endif()
    _moonlight_nxdk_source_state("${source}" signature files)
    set(marker "${destination}/.moonlight-nxdk-copy")
    set(stamp "${destination}/.moonlight-source.signature")
    if(EXISTS "${marker}" AND EXISTS "${stamp}")
        file(READ "${stamp}" previous)
        if(previous STREQUAL signature)
            set_property(GLOBAL PROPERTY MOONLIGHT_STAGED_NXDK "${source}|${destination}")
            return()
        endif()
    endif()

    if(EXISTS "${destination}")
        # Only replace a directory created by this function. Resolve symlinks
        # before deletion so a build-path alias cannot target the SDK checkout.
        if(NOT EXISTS "${marker}")
            message(FATAL_ERROR "Refusing to replace an existing directory not owned by Moonlight: ${destination}")
        endif()
        file(READ "${marker}" owner)
        if(NOT owner STREQUAL "Moonlight nxdk build copy\n")
            message(FATAL_ERROR "Refusing to replace a directory with an invalid ownership marker: ${destination}")
        endif()
        get_filename_component(resolved_destination "${destination}" REALPATH)
        cmake_path(IS_PREFIX source "${resolved_destination}" NORMALIZE inside_source)
        if(inside_source OR NOT resolved_destination STREQUAL destination)
            message(FATAL_ERROR "Refusing to replace an aliased nxdk build directory: ${destination}")
        endif()
        file(REMOVE_RECURSE "${destination}")
    endif()
    message(STATUS "Copying nxdk tracked sources to ${destination}")
    file(MAKE_DIRECTORY "${destination}")
    file(WRITE "${marker}" "Moonlight nxdk build copy\n")
    foreach(file IN LISTS files)
        if(EXISTS "${source}/${file}" OR IS_SYMLINK "${source}/${file}")
            get_filename_component(parent "${destination}/${file}" DIRECTORY)
            file(COPY "${source}/${file}" DESTINATION "${parent}")
        endif()
    endforeach()
    file(WRITE "${stamp}" "${signature}")
    set_property(GLOBAL PROPERTY MOONLIGHT_STAGED_NXDK "${source}|${destination}")
endfunction()
