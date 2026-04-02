# Configure or build the internal Xbox child project inside the vendored nxdk environment.

foreach(required_var
        MOONLIGHT_COMMAND_MODE
        MOONLIGHT_CMAKE_COMMAND
        MOONLIGHT_DESCRIPTION
        MOONLIGHT_NXDK_DIR
        MOONLIGHT_BINARY_DIR
        MOONLIGHT_WORKING_DIRECTORY)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "${required_var} must be defined when running run-child-build.cmake")
    endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/nxdk.cmake")

if(MOONLIGHT_COMMAND_MODE STREQUAL "configure")
    foreach(required_var MOONLIGHT_SOURCE_DIR MOONLIGHT_MAKE_PROGRAM MOONLIGHT_TOOLCHAIN_FILE)
        if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
            message(FATAL_ERROR "${required_var} must be defined for configure mode")
        endif()
    endforeach()

    set(_command_args
            "${MOONLIGHT_CMAKE_COMMAND}"
            -G "Unix Makefiles"
            "-DCMAKE_MAKE_PROGRAM:FILEPATH=${MOONLIGHT_MAKE_PROGRAM}"
            -S "${MOONLIGHT_SOURCE_DIR}"
            -B "${MOONLIGHT_BINARY_DIR}"
            -DMOONLIGHT_BUILD_KIND:STRING=XBOX
            -DMOONLIGHT_SKIP_NXDK_PREP:BOOL=ON
            "-DNXDK_DIR:PATH=${MOONLIGHT_NXDK_DIR}"
            -DBUILD_DOCS:BOOL=OFF
            "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${MOONLIGHT_TOOLCHAIN_FILE}"
            -DCMAKE_DEPENDS_USE_COMPILER:BOOL=FALSE
            -DCMAKE_TRY_COMPILE_TARGET_TYPE:STRING=STATIC_LIBRARY
    )

    if(DEFINED MOONLIGHT_BUILD_TYPE AND NOT MOONLIGHT_BUILD_TYPE STREQUAL "")
        list(APPEND _command_args "-DCMAKE_BUILD_TYPE:STRING=${MOONLIGHT_BUILD_TYPE}")
    endif()
elseif(MOONLIGHT_COMMAND_MODE STREQUAL "build")
    set(_command_args
            "${MOONLIGHT_CMAKE_COMMAND}"
            --build "${MOONLIGHT_BINARY_DIR}"
    )
else()
    message(FATAL_ERROR "Unsupported MOONLIGHT_COMMAND_MODE: ${MOONLIGHT_COMMAND_MODE}")
endif()

moonlight_run_nxdk_command(
        "${MOONLIGHT_DESCRIPTION}"
        "${MOONLIGHT_NXDK_DIR}"
        "${MOONLIGHT_WORKING_DIRECTORY}"
        ${_command_args}
)
