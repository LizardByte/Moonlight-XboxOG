#pragma once

// standard includes
#include <cstdint>
#include <string>

namespace platform {

  char preferred_path_separator();

  bool is_path_separator(char character);

  std::string join_path(const std::string &left, const std::string &right);

  std::string parent_directory(const std::string &filePath);

  bool ensure_directory_exists(const std::string &directoryPath, std::string *errorMessage = nullptr);

  bool try_get_file_size(const std::string &path, std::uint64_t *sizeBytes = nullptr);

  bool path_has_prefix(const std::string &path, const std::string &prefix);

}  // namespace platform
