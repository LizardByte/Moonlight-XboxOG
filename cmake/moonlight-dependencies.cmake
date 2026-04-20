include_guard(GLOBAL)

# Normalize FFmpeg configure probes that incorrectly succeed against the nxdk toolchain.
function(_moonlight_patch_ffmpeg_config_header ffmpeg_config_header)
    if(NOT EXISTS "${ffmpeg_config_header}")
        return()
    endif()

    file(READ "${ffmpeg_config_header}" ffmpeg_config_text)

    foreach(ffmpeg_disabled_probe
            IN ITEMS
            HAVE_ALIGNED_MALLOC
            HAVE_EXP2
            HAVE_EXP2F
            HAVE_LLRINT
            HAVE_LLRINTF
            HAVE_LOG2
            HAVE_LOG2F
            HAVE_LRINT
            HAVE_LRINTF
            HAVE_MEMALIGN
            HAVE_MMAP
            HAVE_POSIX_MEMALIGN
            HAVE_RINT
            HAVE_RINTF
            HAVE_SCHED_GETAFFINITY
            HAVE_STRERROR_R
            HAVE_SYSCTL)
        string(REGEX REPLACE
                "#define ${ffmpeg_disabled_probe} [0-9]+"
                "#define ${ffmpeg_disabled_probe} 0"
                ffmpeg_config_text
                "${ffmpeg_config_text}")
    endforeach()

    file(WRITE "${ffmpeg_config_header}" "${ffmpeg_config_text}")
endfunction()

# Prepare the static FFmpeg libraries used by the Xbox streaming runtime.
function(moonlight_prepare_xbox_ffmpeg nxdk_dir)
    set(ffmpeg_source_dir "${CMAKE_SOURCE_DIR}/third-party/ffmpeg")
    set(ffmpeg_cc_wrapper "${CMAKE_SOURCE_DIR}/scripts/ffmpeg-nxdk-cc.sh")
    set(ffmpeg_cxx_wrapper "${CMAKE_SOURCE_DIR}/scripts/ffmpeg-nxdk-cxx.sh")
    set(ffmpeg_compat_header "${CMAKE_SOURCE_DIR}/src/_nxdk_compat/ffmpeg_compat.h")
    if(NOT EXISTS "${ffmpeg_source_dir}/configure")
        message(FATAL_ERROR
                "FFmpeg source directory not found: ${ffmpeg_source_dir}\n"
                "Run: git submodule update --init --recursive")
    endif()

    foreach(ffmpeg_support_file
            IN ITEMS
            "${ffmpeg_cc_wrapper}"
            "${ffmpeg_cxx_wrapper}"
            "${ffmpeg_compat_header}")
        if(NOT EXISTS "${ffmpeg_support_file}")
            message(FATAL_ERROR "Required FFmpeg support file not found: ${ffmpeg_support_file}")
        endif()
    endforeach()

    set(ffmpeg_state_dir "${CMAKE_BINARY_DIR}/third-party/ffmpeg")
    set(ffmpeg_build_dir "${ffmpeg_state_dir}/build")
    set(ffmpeg_install_dir "${ffmpeg_state_dir}/install")
    set(signature_file "${ffmpeg_state_dir}/build.signature")

    execute_process(
            COMMAND git -C "${ffmpeg_source_dir}" rev-parse HEAD
            OUTPUT_VARIABLE ffmpeg_revision
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE ffmpeg_revision_result
    )
    if(NOT ffmpeg_revision_result EQUAL 0)
        set(ffmpeg_revision unknown)
    endif()

    file(SHA256 "${ffmpeg_cc_wrapper}" ffmpeg_cc_wrapper_hash)
    file(SHA256 "${ffmpeg_cxx_wrapper}" ffmpeg_cxx_wrapper_hash)
    file(SHA256 "${ffmpeg_compat_header}" ffmpeg_compat_header_hash)
    file(SHA256 "${CMAKE_SOURCE_DIR}/cmake/moonlight-dependencies.cmake" ffmpeg_dependency_manifest_hash)

    set(signature_inputs
            "FFMPEG_REVISION=${ffmpeg_revision}"
            "NXDK_DIR=${nxdk_dir}"
            "FFMPEG_PROFILE=h264-opus-xbox"
            "FFMPEG_TARGET_OS=none"
            "FFMPEG_ARCH=x86"
            "FFMPEG_CC_WRAPPER_SHA256=${ffmpeg_cc_wrapper_hash}"
            "FFMPEG_CXX_WRAPPER_SHA256=${ffmpeg_cxx_wrapper_hash}"
            "FFMPEG_COMPAT_HEADER_SHA256=${ffmpeg_compat_header_hash}"
            "FFMPEG_DEPENDENCY_MANIFEST_SHA256=${ffmpeg_dependency_manifest_hash}")
    list(JOIN signature_inputs "\n" signature_text)
    string(SHA256 signature "${signature_text}")

    set(required_outputs
            "${ffmpeg_install_dir}/include/libavcodec/avcodec.h"
            "${ffmpeg_install_dir}/lib/libavcodec.a"
            "${ffmpeg_install_dir}/lib/libavutil.a"
            "${ffmpeg_install_dir}/lib/libswscale.a"
            "${ffmpeg_install_dir}/lib/libswresample.a")

    file(MAKE_DIRECTORY "${ffmpeg_state_dir}")

    set(need_rebuild FALSE)
    if(NOT EXISTS "${signature_file}")
        set(need_rebuild TRUE)
    else()
        file(READ "${signature_file}" saved_signature)
        string(STRIP "${saved_signature}" saved_signature)
        if(NOT saved_signature STREQUAL signature)
            set(need_rebuild TRUE)
        endif()
    endif()

    _moonlight_has_missing_output(ffmpeg_missing_output ${required_outputs})
    if(ffmpeg_missing_output)
        set(need_rebuild TRUE)
    endif()

    if(need_rebuild)
        message(STATUS "Preparing FFmpeg for Xbox at ${ffmpeg_build_dir}")
        file(REMOVE_RECURSE "${ffmpeg_build_dir}" "${ffmpeg_install_dir}")
        file(MAKE_DIRECTORY "${ffmpeg_build_dir}")

        if(CMAKE_HOST_WIN32)
            _moonlight_to_msys_path(ffmpeg_source_shell_path "${ffmpeg_source_dir}")
            _moonlight_to_msys_path(ffmpeg_install_shell_path "${ffmpeg_install_dir}")
            _moonlight_to_msys_path(ffmpeg_build_shell_path "${ffmpeg_build_dir}")
            _moonlight_to_msys_path(nxdk_shell_path "${nxdk_dir}")
            _moonlight_to_msys_path(ffmpeg_cc_shell_path "${ffmpeg_cc_wrapper}")
            _moonlight_to_msys_path(ffmpeg_cxx_shell_path "${ffmpeg_cxx_wrapper}")
        else()
            set(ffmpeg_source_shell_path "${ffmpeg_source_dir}")
            set(ffmpeg_install_shell_path "${ffmpeg_install_dir}")
            set(ffmpeg_cc_shell_path nxdk-cc)
            set(ffmpeg_cxx_shell_path nxdk-cxx)
        endif()

        set(ffmpeg_configure_args
                sh
                "${ffmpeg_source_shell_path}/configure"
                "--prefix=${ffmpeg_install_shell_path}"
                --enable-cross-compile
                --arch=x86
                --cpu=i686
                --target-os=none
                "--cc=${ffmpeg_cc_shell_path}"
                "--cxx=${ffmpeg_cxx_shell_path}"
                --ar=llvm-ar
                --ranlib=llvm-ranlib
                --nm=llvm-nm
                --enable-static
                --disable-shared
                --disable-autodetect
                --disable-asm
                --disable-inline-asm
                --disable-x86asm
                --disable-debug
                --disable-doc
                --disable-programs
                --disable-network
                --disable-everything
                --disable-avdevice
                --disable-avfilter
                --disable-avformat
                --disable-iconv
                --disable-zlib
                --disable-bzlib
                --disable-lzma
                --disable-sdl2
                --disable-symver
                --disable-runtime-cpudetect
                --disable-pthreads
                --disable-w32threads
                --disable-os2threads
                --disable-hwaccels
                --enable-avcodec
                --enable-avutil
                --enable-swscale
                --enable-swresample
                --enable-parser=h264
                --enable-decoder=h264
                --enable-decoder=opus)

        if(CMAKE_HOST_WIN32)
            set(msys2_shell "C:/msys64/msys2_shell.cmd")
            if(NOT EXISTS "${msys2_shell}")
                message(FATAL_ERROR "MSYS2 shell not found at ${msys2_shell}")
            endif()
            _moonlight_join_shell_command(ffmpeg_configure_command ${ffmpeg_configure_args})
            _moonlight_join_shell_command(ffmpeg_build_command make -j4 install)
            _moonlight_shell_quote(quoted_nxdk_shell_path "${nxdk_shell_path}")
            _moonlight_shell_quote(quoted_ffmpeg_build_shell_path "${ffmpeg_build_shell_path}")

            string(CONCAT ffmpeg_configure_script
                    "unset MAKEFLAGS MFLAGS GNUMAKEFLAGS MAKELEVEL; "
                    "export NXDK_DIR=${quoted_nxdk_shell_path}; "
                    "export PATH=\"$NXDK_DIR/bin:$PATH\"; "
                    "cd ${quoted_ffmpeg_build_shell_path}; "
                    "exec ${ffmpeg_configure_command}")
            execute_process(
                    COMMAND "${msys2_shell}" -defterm -here -no-start -mingw64 -c "${ffmpeg_configure_script}"
                    RESULT_VARIABLE ffmpeg_configure_result
            )
            if(NOT ffmpeg_configure_result EQUAL 0)
                message(FATAL_ERROR "FFmpeg configure failed with exit code ${ffmpeg_configure_result}")
            endif()

            set(ffmpeg_config_header "${ffmpeg_build_dir}/config.h")
            _moonlight_patch_ffmpeg_config_header("${ffmpeg_config_header}")

            string(CONCAT ffmpeg_build_script
                    "unset MAKEFLAGS MFLAGS GNUMAKEFLAGS MAKELEVEL; "
                    "export NXDK_DIR=${quoted_nxdk_shell_path}; "
                    "export PATH=\"$NXDK_DIR/bin:$PATH\"; "
                    "cd ${quoted_ffmpeg_build_shell_path}; "
                    "exec ${ffmpeg_build_command}")
            execute_process(
                    COMMAND "${msys2_shell}" -defterm -here -no-start -mingw64 -c "${ffmpeg_build_script}"
                    RESULT_VARIABLE ffmpeg_build_result
            )
            if(NOT ffmpeg_build_result EQUAL 0)
                message(FATAL_ERROR "FFmpeg build failed with exit code ${ffmpeg_build_result}")
            endif()
        else()
            moonlight_run_nxdk_command(
                    "FFmpeg configure"
                    "${nxdk_dir}"
                    "${ffmpeg_build_dir}"
                    ${ffmpeg_configure_args}
            )
            set(ffmpeg_config_header "${ffmpeg_build_dir}/config.h")
            _moonlight_patch_ffmpeg_config_header("${ffmpeg_config_header}")
            moonlight_run_nxdk_command(
                    "FFmpeg build"
                    "${nxdk_dir}"
                    "${ffmpeg_build_dir}"
                    make
                    -j4
                    install
            )
        endif()

        file(WRITE "${signature_file}" "${signature}\n")
    else()
        message(STATUS "Using existing FFmpeg Xbox outputs from ${ffmpeg_install_dir}")
    endif()

    set(MOONLIGHT_FFMPEG_INCLUDE_DIR "${ffmpeg_install_dir}/include" PARENT_SCOPE)
    set(MOONLIGHT_FFMPEG_LIBRARIES
            "${ffmpeg_install_dir}/lib/libavcodec.a"
            "${ffmpeg_install_dir}/lib/libswscale.a"
            "${ffmpeg_install_dir}/lib/libswresample.a"
            "${ffmpeg_install_dir}/lib/libavutil.a"
            PARENT_SCOPE)
endfunction()

# Prepare dependencies that are common to multiple Moonlight components
macro(MOONLIGHT_PREPARE_COMMON_DEPENDENCIES)
    include(GetOpenSSL REQUIRED)

    if(NOT TARGET moonlight-openssl)
        add_library(moonlight-openssl INTERFACE)
        add_library(Moonlight::OpenSSL ALIAS moonlight-openssl)
        target_link_libraries(moonlight-openssl
                INTERFACE
                OpenSSL::SSL
                OpenSSL::Crypto)
    endif()

    set(ENET_NO_INSTALL ON CACHE BOOL "Do not install libraries built for enet" FORCE)

    set(_moonlight_restore_build_shared_libs FALSE)
    if(DEFINED BUILD_SHARED_LIBS)
        set(_moonlight_restore_build_shared_libs TRUE)
        set(_moonlight_saved_build_shared_libs "${BUILD_SHARED_LIBS}")
    endif()

    set(BUILD_SHARED_LIBS OFF)

    if(NOT TARGET moonlight-common-c)
        add_subdirectory(
                "${CMAKE_SOURCE_DIR}/third-party/moonlight-common-c"
                "${CMAKE_BINARY_DIR}/third-party/moonlight-common-c"
        )
    endif()

    if(_moonlight_restore_build_shared_libs)
        set(BUILD_SHARED_LIBS "${_moonlight_saved_build_shared_libs}")
    else()
        unset(BUILD_SHARED_LIBS)
    endif()

    if(TARGET moonlight-common-c AND DEFINED MOONLIGHT_OPENSSL_EXTERNAL_TARGET)
        if(TARGET ${MOONLIGHT_OPENSSL_EXTERNAL_TARGET})
            add_dependencies(moonlight-common-c ${MOONLIGHT_OPENSSL_EXTERNAL_TARGET})
        endif()
    endif()

    if(TARGET moonlight-common-c
            AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_compile_options(moonlight-common-c PRIVATE -Wno-error=cast-function-type)
    endif()

    if(MOONLIGHT_BUILD_KIND STREQUAL "XBOX")
        if(NOT DEFINED NXDK_DIR OR NXDK_DIR STREQUAL "")
            message(FATAL_ERROR "NXDK_DIR must be defined before preparing Xbox dependencies")
        endif()

        set(MOONLIGHT_NXDK_NET_INCLUDE_DIR "${NXDK_DIR}/lib/net")
        set(MOONLIGHT_NXDK_LIBC_EXTENSIONS_DIR "${NXDK_DIR}/lib/xboxrt/libc_extensions")
        set(MOONLIGHT_NXDK_LWIP_POSIX_COMPAT_DIR "${NXDK_DIR}/lib/net/lwip/src/include/compat/posix")
        set(MOONLIGHT_NXDK_LWIP_MDNS_SOURCES
                "${NXDK_DIR}/lib/net/lwip/src/apps/mdns/mdns.c"
                "${NXDK_DIR}/lib/net/lwip/src/apps/mdns/mdns_domain.c"
                "${NXDK_DIR}/lib/net/lwip/src/apps/mdns/mdns_out.c"
        )

        if(TARGET enet)
            target_compile_definitions(enet PRIVATE NXDK)
            target_link_libraries(enet PUBLIC NXDK::NXDK NXDK::Net)
            target_include_directories(enet PRIVATE
                    "${MOONLIGHT_NXDK_NET_INCLUDE_DIR}"
                    "${MOONLIGHT_NXDK_LIBC_EXTENSIONS_DIR}"
                    "${MOONLIGHT_NXDK_LWIP_POSIX_COMPAT_DIR}")
            target_compile_options(enet PRIVATE -Wno-unused-function -Wno-error=unused-function)
        endif()

        if(TARGET moonlight-common-c)
            target_compile_definitions(moonlight-common-c PRIVATE NXDK)
            target_include_directories(moonlight-common-c PRIVATE
                    "${MOONLIGHT_NXDK_NET_INCLUDE_DIR}"
                    "${MOONLIGHT_NXDK_LIBC_EXTENSIONS_DIR}"
                    "${MOONLIGHT_NXDK_LWIP_POSIX_COMPAT_DIR}")
            target_compile_options(moonlight-common-c PRIVATE
                    -Wno-unused-function
                    -Wno-error=unused-function)
        endif()

        moonlight_prepare_xbox_ffmpeg("${NXDK_DIR}")
    endif()
endmacro()
