# Build Moonlight's adapters against unmodified upstream nxdk sources.
include_guard(GLOBAL)

# Apply the network patch to disposable copies, then publish only changed files.
function(_moonlight_patch_nxdk_network port patch_file)
    find_package(Git REQUIRED)
    set(work "${port}/patch-work")
    set(port_files
            lib/net/nforceif/include/arch/sys_arch.h
            lib/net/nforceif/src/sys_arch.c
            lib/net/nvnetdrv/nvnetdrv_lwip.c)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${patch_file}")
    foreach(source IN LISTS port_files)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${NXDK_DIR}/${source}")
        configure_file("${NXDK_DIR}/${source}" "${work}/${source}" COPYONLY)
    endforeach()

    # An explicit work tree confines git apply to these copies, including when
    # the build directory is inside the Moonlight checkout. Normalize CRLF on
    # input and write LF so patching is independent of the user's Git settings.
    execute_process(
            COMMAND "${GIT_EXECUTABLE}" -c core.autocrlf=input "--work-tree=${work}"
                apply --whitespace=error "${patch_file}"
            WORKING_DIRECTORY "${work}"
            RESULT_VARIABLE patch_result
            OUTPUT_VARIABLE patch_output
            ERROR_VARIABLE patch_error)
    if(NOT patch_result EQUAL 0)
        message(FATAL_ERROR
                "Failed to apply Moonlight's nxdk network patch; review it against the selected SDK.\n"
                "${patch_output}${patch_error}")
    endif()
    foreach(source IN LISTS port_files)
        configure_file("${work}/${source}" "${port}/${source}" COPYONLY)
    endforeach()
endfunction()

function(moonlight_prepare_nxdk_compat)
    set(compat "${CMAKE_SOURCE_DIR}/src/_nxdk_compat")
    set(port "${CMAKE_CURRENT_BINARY_DIR}/nxdk-compat")
    _moonlight_patch_nxdk_network("${port}" "${compat}/network/nxdk-network.patch")

    # opt.h includes this before defining defaults. Keep nxdk's base settings.
    configure_file("${compat}/network/lwipopts.h.in" "${port}/lwipopts.h" @ONLY)

    set(LWIP_DIR "${NXDK_DIR}/lib/net/lwip")
    set(LWIP_EXCLUDE_SLIPIF TRUE)
    include("${LWIP_DIR}/src/Filelists.cmake")
    add_library(moonlight_nxdk_net STATIC
            ${lwipcore_SRCS} ${lwipcore4_SRCS} ${lwipcore6_SRCS}
            ${lwipapi_SRCS} ${lwipnetif_SRCS} ${lwipmdns_SRCS}
            "${NXDK_DIR}/lib/net/nvnetdrv/nvnetdrv.c"
            "${port}/lib/net/nvnetdrv/nvnetdrv_lwip.c"
            "${port}/lib/net/nforceif/src/sys_arch.c")
    target_compile_definitions(moonlight_nxdk_net PRIVATE RX_BUFF_CNT=PBUF_POOL_SIZE)
    target_include_directories(moonlight_nxdk_net BEFORE PUBLIC
            "${port}/lib/net/nforceif/include" "${port}" "${compat}/include")
    target_include_directories(moonlight_nxdk_net SYSTEM PUBLIC
            "${NXDK_DIR}/lib/net/lwip/src/include"
            "${NXDK_DIR}/lib/net/nforceif/include"
            "${NXDK_DIR}/lib/net/nvnetdrv")

    # nxNetInit owns a struct netif: it must use the same mDNS-enabled layout.
    # Include it as an object so the stock libnxdk net.obj is never extracted.
    add_library(moonlight_nxdk_net_init OBJECT "${NXDK_DIR}/lib/nxdk/net.c")
    target_link_libraries(moonlight_nxdk_net_init PRIVATE moonlight_nxdk_net)
    get_target_property(sdk_libraries NXDK::NXDK INTERFACE_LINK_LIBRARIES)
    list(REMOVE_ITEM sdk_libraries nxdk_net)
    set_property(TARGET NXDK::NXDK PROPERTY INTERFACE_LINK_LIBRARIES "${sdk_libraries};moonlight_nxdk_net")
    set_property(TARGET NXDK::Net PROPERTY INTERFACE_LINK_LIBRARIES moonlight_nxdk_net)

    # FFmpeg configures before ordinary targets build. Build its real timing
    # helpers now as well, so configure still links genuine Xbox executables.
    moonlight_run_nxdk_command("Compatibility probe object" "${NXDK_DIR}" "${port}"
            "${NXDK_DIR}/bin/nxdk-cc" "-I${compat}/include"
            -c "${compat}/runtime.c" -o runtime.obj)
    moonlight_run_nxdk_command("Compatibility probe archive" "${NXDK_DIR}" "${port}"
            llvm-ar rcs compat.lib runtime.obj)
    set(MOONLIGHT_NXDK_COMPAT_LIBRARY "${port}/compat.lib" PARENT_SCOPE)
    add_library(moonlight_nxdk_compat STATIC "${compat}/runtime.c")
    target_include_directories(moonlight_nxdk_compat PUBLIC "${compat}/include")
    target_compile_options(moonlight_nxdk_compat INTERFACE
            -include "${compat}/include/moonlight_posix.h")
    file(GLOB_RECURSE compat_inputs CONFIGURE_DEPENDS "${compat}/*.h" "${compat}/*.c")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${compat_inputs})
endfunction()
