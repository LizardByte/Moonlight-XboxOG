// class header include
#include "src/startup/storage_paths.h"

// standard includes
#include <string>

// nxdk includes
#if defined(__has_include) && __has_include(<nxdk/xbe.h>)
  #include <nxdk/xbe.h>
  #if __has_include(<nxdk/mount.h>)
    #include <nxdk/mount.h>
  #endif
  #include <array>
  #include <cstdio>
  #include <winnt.h>
#endif

namespace startup {

  std::string title_scoped_storage_root() {
#if defined(__has_include) && __has_include(<nxdk/xbe.h>)
  #if __has_include(<nxdk/mount.h>)
    if (!nxIsDriveMounted('E') && !nxMountDrive('E', "\\Device\\Harddisk0\\Partition1\\")) {
      return {};
    }
  #endif

    std::array<char, 9> titleIdBuffer {};
    std::snprintf(titleIdBuffer.data(), titleIdBuffer.size(), "%08X", CURRENT_XBE_HEADER->CertificateHeader->TitleID);
    return std::string("E:\\UDATA\\") + titleIdBuffer.data() + "\\";
#else
    return {};
#endif
  }

  std::string default_storage_path(std::string_view entryName) {
    if (const std::string titleScopedRoot = title_scoped_storage_root(); !titleScopedRoot.empty()) {
      return titleScopedRoot + std::string(entryName);
    }

    return std::string(entryName);
  }

}  // namespace startup
