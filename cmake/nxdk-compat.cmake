# Build Moonlight's adapters against unmodified upstream nxdk sources.
include_guard(GLOBAL)

# Apply a checked port adjustment to a generated copy, never to the submodule.
function(_moonlight_replace_port_text variable before after)
    string(FIND "${${variable}}" "${before}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "nxdk port changed; review Moonlight's network adapter: ${before}")
    endif()
    string(REPLACE "${before}" "${after}" result "${${variable}}")
    set(${variable} "${result}" PARENT_SCOPE)
endfunction()

function(moonlight_prepare_nxdk_compat)
    set(compat "${CMAKE_SOURCE_DIR}/src/_nxdk_compat")
    set(port "${CMAKE_CURRENT_BINARY_DIR}/nxdk-compat")
    file(MAKE_DIRECTORY "${port}/arch")

    # opt.h includes this before defining defaults. Keep nxdk's base settings.
    configure_file("${compat}/network/lwipopts.h.in" "${port}/lwipopts.h" @ONLY)
    set(sys_header "${NXDK_DIR}/lib/net/nforceif/include/arch/sys_arch.h")
    set(sys_source "${NXDK_DIR}/lib/net/nforceif/src/sys_arch.c")
    set(rx_source "${NXDK_DIR}/lib/net/nvnetdrv/nvnetdrv_lwip.c")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            "${sys_header}" "${sys_source}" "${rx_source}")
    file(READ "${sys_header}" text)
    _moonlight_replace_port_text(text "#define SYS_MBOX_SIZE 128" "#define SYS_MBOX_SIZE 255")
    file(CONFIGURE OUTPUT "${port}/arch/sys_arch.h" CONTENT "${text}" @ONLY)

    file(READ "${sys_source}" text)
    _moonlight_replace_port_text(text "#include <stdlib.h>" "#include <stdlib.h>\n#include <windows.h>")
    _moonlight_replace_port_text(text "    LWIP_UNUSED_ARG(prio);" "")
    _moonlight_replace_port_text(text
            "    return CreateThread(NULL, stacksize, (void *)function, arg, 0, NULL);"
            "    HANDLE thread = CreateThread(NULL, stacksize, (void *)function, arg, 0, NULL);\n    if (thread != NULL && prio != 0) {\n        SetThreadPriority(thread, prio);\n    }\n    return thread;")
    file(CONFIGURE OUTPUT "${port}/sys_arch.c" CONTENT "${text}" @ONLY)

    file(READ "${rx_source}" text)
    _moonlight_replace_port_text(text
            "    LWIP_ASSERT(\"RX_POOL full\\n\", rx_pbuf != NULL);"
            "    if (rx_pbuf == NULL) {\n        LINK_STATS_INC(link.memerr);\n        LINK_STATS_INC(link.drop);\n        nvnetdrv_rx_release(buffer);\n        return;\n    }")
    _moonlight_replace_port_text(text
            "    if (g_pnetif->input(p, g_pnetif) != ERR_OK) {"
            "    if (g_pnetif->input(p, g_pnetif) != ERR_OK) {\n        LINK_STATS_INC(link.memerr);\n        LINK_STATS_INC(link.drop);")
    file(CONFIGURE OUTPUT "${port}/nvnetdrv_lwip.c" CONTENT "${text}" @ONLY)

    set(LWIP_DIR "${NXDK_DIR}/lib/net/lwip")
    set(LWIP_EXCLUDE_SLIPIF TRUE)
    include("${LWIP_DIR}/src/Filelists.cmake")
    add_library(moonlight_nxdk_net STATIC
            ${lwipcore_SRCS} ${lwipcore4_SRCS} ${lwipcore6_SRCS}
            ${lwipapi_SRCS} ${lwipnetif_SRCS} ${lwipmdns_SRCS}
            "${NXDK_DIR}/lib/net/nvnetdrv/nvnetdrv.c"
            "${port}/nvnetdrv_lwip.c" "${port}/sys_arch.c")
    target_compile_definitions(moonlight_nxdk_net PRIVATE RX_BUFF_CNT=PBUF_POOL_SIZE)
    target_include_directories(moonlight_nxdk_net BEFORE PUBLIC
            "${port}" "${compat}/include")
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
