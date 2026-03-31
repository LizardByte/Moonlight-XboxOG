include("${CMAKE_CURRENT_LIST_DIR}/../third-party/nxdk/share/toolchain-nxdk.cmake")

if(CMAKE_HOST_WIN32)
    set(CMAKE_C_COMPILER "${CMAKE_CURRENT_LIST_DIR}/nxdk-cc.bat" CACHE FILEPATH "" FORCE)
    set(CMAKE_CXX_COMPILER "${CMAKE_CURRENT_LIST_DIR}/nxdk-cxx.bat" CACHE FILEPATH "" FORCE)
    set(PKG_CONFIG_EXECUTABLE "${CMAKE_CURRENT_LIST_DIR}/nxdk-pkg-config.bat" CACHE STRING "Path to pkg-config" FORCE)
endif()
