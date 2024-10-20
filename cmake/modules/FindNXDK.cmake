if(NOT TARGET NXDK::NXDK)
    add_library(nxdk STATIC IMPORTED)
    set_target_properties(
            nxdk
            PROPERTIES
            IMPORTED_LOCATION "${NXDK_DIR}/lib/libnxdk.lib"
    )

    add_library(nxdk_automount_d STATIC IMPORTED)
    set_target_properties(
            nxdk_automount_d
            PROPERTIES
            IMPORTED_LOCATION "${NXDK_DIR}/lib/libnxdk_automount_d.lib"
    )

    add_library(nxdk_hal STATIC IMPORTED)
    set_target_properties(
            nxdk_hal
            PROPERTIES
            IMPORTED_LOCATION "${NXDK_DIR}/lib/libnxdk_hal.lib"
    )

    add_library(nxdk_net STATIC IMPORTED)
    set_target_properties(
            nxdk_net
            PROPERTIES
            IMPORTED_LOCATION "${NXDK_DIR}/lib/libnxdk_net.lib"
    )

    add_library(nxdk_usb STATIC IMPORTED)
    set_target_properties(
            nxdk_usb
            PROPERTIES
            IMPORTED_LOCATION "${NXDK_DIR}/lib/nxdk_usb.lib"
    )

    add_library(pbkit STATIC IMPORTED)
    set_target_properties(
            pbkit
            PROPERTIES
            IMPORTED_LOCATION "${NXDK_DIR}/lib/libpbkit.lib"
    )

    add_library(pdclib STATIC IMPORTED)
    set_target_properties(
            pdclib
            PROPERTIES
            IMPORTED_LOCATION "${NXDK_DIR}/lib/libpdclib.lib"
    )

    add_library(winapi STATIC IMPORTED)
    set_target_properties(
            winapi
            PROPERTIES
            IMPORTED_LOCATION "${NXDK_DIR}/lib/libwinapi.lib"
    )

    add_library(winmm STATIC IMPORTED)
    set_target_properties(
            winmm
            PROPERTIES
            IMPORTED_LOCATION "${NXDK_DIR}/lib/winmm.lib"
    )

    add_library(ws2_32 STATIC IMPORTED)
    set_target_properties(
            ws2_32
            PROPERTIES
            IMPORTED_LOCATION "${NXDK_DIR}/lib/ws2_32.lib"
    )

    add_library(xboxrt STATIC IMPORTED)
    set_target_properties(
            xboxrt
            PROPERTIES
            IMPORTED_LOCATION "${NXDK_DIR}/lib/libxboxrt.lib"
    )

    add_library(zlib STATIC IMPORTED)
    set_target_properties(
            zlib
            PROPERTIES
            IMPORTED_LOCATION "${NXDK_DIR}/lib/libzlib.lib"
    )

    add_library(NXDK::NXDK INTERFACE IMPORTED)
    target_link_libraries(
            NXDK::NXDK
            INTERFACE
            nxdk
            nxdk_automount_d
            nxdk_hal
            nxdk_net
            nxdk_usb
            pbkit
            pdclib
            winapi
            winmm
            ws2_32
            xboxrt
            zlib
    )
endif()

if(NOT TARGET NXDK::NXDK_CXX)

    add_library(nxdk_cxx STATIC IMPORTED)
    set_target_properties(
            nxdk_cxx
            PROPERTIES
            IMPORTED_LOCATION "${NXDK_DIR}/lib/libc++.lib"
    )

    add_library(NXDK::NXDK_CXX INTERFACE IMPORTED)
    target_link_libraries(NXDK::NXDK_CXX INTERFACE nxdk_cxx)
endif()

if (NOT TARGET NXDK::Net)
    add_library(NXDK::Net INTERFACE IMPORTED)
    target_link_libraries(
            NXDK::Net
            INTERFACE
            nxdk_net
    )
    target_include_directories(
            NXDK::Net
            SYSTEM INTERFACE
            "${NXDK_DIR}/lib/net/lwip/src/include"
            "${NXDK_DIR}/lib/net/nforceif/include"
            "${NXDK_DIR}/lib/net/nvnetdrv"
            "${NXDK_DIR}/lib/net/pktdrv"
    )
endif ()

if (NOT TARGET NXDK::ws2_32)
    add_library(NXDK::ws2_32 INTERFACE IMPORTED)
    target_link_libraries(
            NXDK::ws2_32
            INTERFACE
            ws2_32
    )
    target_include_directories(
            NXDK::ws2_32
            SYSTEM INTERFACE
            "${NXDK_DIR}/lib/winapi/ws2_32"
    )
endif ()
