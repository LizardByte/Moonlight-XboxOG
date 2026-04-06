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
    std::string path;
    std::string displayName;
    std::uint64_t sizeBytes = 0;
  };

  /**
   * @brief Optional path overrides used to inspect Moonlight-managed files.
   */
  struct SavedFileCatalogConfig {
    std::string hostStoragePath;
    std::string logFilePath;
    std::string pairingDirectory;
    std::string coverArtCacheRoot;
  };

  /**
   * @brief Result of enumerating Moonlight-managed files on disk.
   */
  struct ListSavedFilesResult {
    std::vector<SavedFileEntry> files;
    std::vector<std::string> warnings;
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


