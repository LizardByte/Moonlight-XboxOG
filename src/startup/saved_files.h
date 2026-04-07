#pragma once

// standard includes
#include <cstdint>
#include <string>
#include <vector>

namespace startup {

  /**
   * @brief Describes one Moonlight-managed file that exists on disk.
   */
  struct SavedFileEntry {
    std::string path;  ///< Absolute or managed-relative path to the file on disk.
    std::string displayName;  ///< User-facing label shown in the settings UI.
    std::uint64_t sizeBytes = 0;  ///< File size in bytes.
  };

  /**
   * @brief Optional path overrides used to inspect Moonlight-managed files.
   */
  struct SavedFileCatalogConfig {
    std::string hostStoragePath;  ///< Path to the saved-host storage file
    std::string logFilePath;  ///< Path to the persisted log file
    std::string pairingDirectory;  ///< Directory containing saved client pairing identity files
    std::string coverArtCacheRoot;  ///< Root directory containing cached cover art artifacts
  };

  /**
   * @brief Result of enumerating Moonlight-managed files on disk.
   */
  struct ListSavedFilesResult {
    std::vector<SavedFileEntry> files;  ///< Managed files currently found on disk.
    std::vector<std::string> warnings;  ///< Non-fatal warnings produced during enumeration.
  };

  /**
   * @brief Enumerate Moonlight-managed files that currently exist on disk.
   *
   * @param config Optional path overrides for tests or custom storage roots.
   * @return Existing files plus any non-fatal enumeration warnings.
   */
  ListSavedFilesResult list_saved_files(const SavedFileCatalogConfig &config = {});

  /**
   * @brief Delete one Moonlight-managed file.
   *
   * @param path Absolute or relative path returned by list_saved_files().
   * @param errorMessage Optional output for deletion failures.
   * @param config Optional path overrides for tests or custom storage roots.
   * @return true when the file was deleted or was already absent.
   */
  bool delete_saved_file(
    const std::string &path,
    std::string *errorMessage = nullptr,
    const SavedFileCatalogConfig &config = {}
  );

  /**
   * @brief Delete every Moonlight-managed saved file currently present on disk.
   *
   * @param errorMessage Optional output for factory-reset failures.
   * @param config Optional path overrides for tests or custom storage roots.
   * @return true when all managed files were removed successfully.
   */
  bool delete_all_saved_files(
    std::string *errorMessage = nullptr,
    const SavedFileCatalogConfig &config = {}
  );

}  // namespace startup
