#pragma once

// standard includes
#include <cstddef>
#include <string>
#include <vector>

// local includes
#include "src/logging/logger.h"

namespace logging {

  struct LoadLogFileResult {
    std::string filePath;
    std::vector<std::string> lines;
    bool fileFound = false;
    std::string errorMessage;
  };

  std::string default_log_file_path();

  bool reset_log_file(const std::string &filePath = default_log_file_path(), std::string *errorMessage = nullptr);

  bool append_log_file_entry(const LogEntry &entry, const std::string &filePath = default_log_file_path(), std::string *errorMessage = nullptr);

  LoadLogFileResult load_log_file(const std::string &filePath = default_log_file_path(), std::size_t maxLines = 64U);

}  // namespace logging
