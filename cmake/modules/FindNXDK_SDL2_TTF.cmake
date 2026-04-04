if(NOT TARGET NXDK::SDL2_TTF)
    add_library(nxdk_sdl2_ttf STATIC IMPORTED)
    set_target_properties(
            nxdk_sdl2_ttf
            PROPERTIES
            IMPORTED_LOCATION "${NXDK_DIR}/lib/libSDL_ttf.lib"
    )

    add_library(nxdk_freetype STATIC IMPORTED)
    set_target_properties(
            nxdk_freetype
            PROPERTIES
            IMPORTED_LOCATION "${NXDK_DIR}/lib/libfreetype.lib"
    )

    add_library(NXDK::SDL2_TTF INTERFACE IMPORTED)
    target_link_libraries(
            NXDK::SDL2_TTF
            INTERFACE
            nxdk_sdl2_ttf
            nxdk_freetype
    )
    target_include_directories(
            NXDK::SDL2_TTF
            SYSTEM INTERFACE
            "${NXDK_DIR}/lib/sdl/SDL_ttf"
            "${NXDK_DIR}/lib/sdl/SDL_ttf/external/freetype-2.4.12/include"
    )
endif()
