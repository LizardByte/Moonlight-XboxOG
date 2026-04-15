/**
 * @file src/startup/host_storage.h
 * @brief Declares saved host persistence.
 */
#pragma once

// standard includes
#include <string>
#include <vector>

// local includes
#include "src/app/host_records.h"

namespace startup {

  /**
   * @brief Result of loading persisted saved hosts from disk.
   */
  struct LoadSavedHostsResult {
    std::vector<app::HostRecord> hosts;  ///< Loaded saved host records.
    std::vector<std::string> warnings;  ///< Non-fatal warnings encountered while loading.
    bool fileFound;  ///< True when the host storage file existed.
  };

  /**
   * @brief Result of saving the current saved-host list to disk.
   */
  struct SaveSavedHostsResult {
    bool success;  ///< True when the host list was saved successfully.
    std::string errorMessage;  ///< Error detail when saving failed.
  };

  /**
   * @brief Return the default path used for saved host storage.
   *
   * @return Default saved-host storage path.
   */
  std::string default_host_storage_path();

  /**
   * @brief Load saved host records from disk.
   *
   * @param filePath Storage file to load.
   * @return Loaded hosts plus any non-fatal warnings.
   */
  LoadSavedHostsResult load_saved_hosts(const std::string &filePath = default_host_storage_path());

  /**
   * @brief Save the current saved host records to disk.
   *
   * @param hosts Saved host records to persist.
   * @param filePath Storage file to write.
   * @return Save result including success state and error detail.
   */
  SaveSavedHostsResult save_saved_hosts(
    const std::vector<app::HostRecord> &hosts,
    const std::string &filePath = default_host_storage_path()
  );

}  // namespace startup
