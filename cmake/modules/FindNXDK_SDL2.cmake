if(NOT TARGET NXDK::SDL2)
    add_library(nxdk_sdl2 STATIC IMPORTED)
    set_target_properties(
            nxdk_sdl2
            PROPERTIES
            IMPORTED_LOCATION "${NXDK_DIR}/lib/libSDL2.lib"
    )

    add_library(NXDK::SDL2 INTERFACE IMPORTED)
    target_link_libraries(
            NXDK::SDL2
            INTERFACE
            nxdk_sdl2
    )
    target_include_directories(
            NXDK::SDL2
            SYSTEM INTERFACE
            "${NXDK_DIR}/lib/sdl/SDL2/include"
    )
endif()
