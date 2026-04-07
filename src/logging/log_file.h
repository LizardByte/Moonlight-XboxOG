#pragma once

// standard includes
#include <cstddef>
#include <string>
#include <vector>

// local includes
#include "src/logging/logger.h"

namespace logging {

  /**
   * @brief Result of loading the persisted log file for the shell viewer.
   */
  struct LoadLogFileResult {
    std::string filePath;  ///< Path that was requested for loading.
    std::vector<std::string> lines;  ///< Loaded log lines in display order.
    bool fileFound = false;  ///< True when the target file existed on disk.
    std::string errorMessage;  ///< Error detail when loading failed.
  };

  /**
   * @brief Return the default path used for persisted log output.
   *
   * @return Default log file path.
   */
  std::string default_log_file_path();

  /**
   * @brief Truncate or recreate the persisted log file.
   *
   * @param filePath Path to reset.
   * @param errorMessage Optional output for I/O failures.
   * @return true when the file was reset successfully.
   */
  bool reset_log_file(const std::string &filePath = default_log_file_path(), std::string *errorMessage = nullptr);

  /**
   * @brief Append one formatted log entry to the persisted log file.
   *
   * @param entry Structured log entry to append.
   * @param filePath Target log file path.
   * @param errorMessage Optional output for I/O failures.
   * @return true when the entry was written successfully.
   */
  bool append_log_file_entry(const LogEntry &entry, const std::string &filePath = default_log_file_path(), std::string *errorMessage = nullptr);

  /**
   * @brief Load recent lines from the persisted log file.
   *
   * @param filePath Target log file path.
   * @param maxLines Maximum number of trailing lines to retain.
   * @return Loaded log file contents and any error details.
   */
  LoadLogFileResult load_log_file(const std::string &filePath = default_log_file_path(), std::size_t maxLines = 64U);

}  // namespace logging
