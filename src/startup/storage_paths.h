#pragma once

// standard includes
#include <string>
#include <string_view>

namespace startup {

  /**
   * @brief Return the title-scoped storage root used for Xbox save data.
   *
   * On nxdk builds this resolves the current title ID under `E:\UDATA\` and
   * mounts the backing drive when needed. On host-native builds it returns an
   * empty string.
   *
   * @return Title-scoped storage root, or an empty string when unavailable.
   */
  std::string title_scoped_storage_root();

  /**
   * @brief Return the default storage path for a file or directory under Moonlight-managed storage.
   *
   * On nxdk builds this prefixes the supplied entry with the title-scoped save root.
   * On host-native builds it returns the entry unchanged so tests can use relative paths.
   *
   * @param entryName File or directory name stored under the Moonlight data root.
   * @return Fully qualified storage path, or the original entry name on host-native builds.
   */
  std::string default_storage_path(std::string_view entryName);

}  // namespace startup
