#pragma once

// standard includes
#include <cstdint>
#include <string>
#include <string_view>

namespace platform {

  /**
   * @brief Return the preferred path separator for the active platform.
   *
   * @return Preferred path separator character.
   */
  char preferred_path_separator();

  /**
   * @brief Return whether a character is recognized as a path separator.
   *
   * @param character Character to inspect.
   * @return True when the character is a supported path separator.
   */
  bool is_path_separator(char character);

  /**
   * @brief Join two path components using the preferred platform separator.
   *
   * @param left Left path component.
   * @param right Right path component.
   * @return Joined path.
   */
  std::string join_path(const std::string &left, const std::string &right);

  /**
   * @brief Return the parent directory portion of a file path.
   *
   * @param filePath File path to inspect.
   * @return Parent directory path, or an empty string when none exists.
   */
  std::string parent_directory(std::string_view filePath);

  /**
   * @brief Return the last path component from a file path.
   *
   * @param path Path to inspect.
   * @return Final path component, or the original string when no separator exists.
   */
  std::string file_name_from_path(std::string_view path);

  /**
   * @brief Ensure that a directory path exists, creating missing segments as needed.
   *
   * @param directoryPath Directory path to create.
   * @param errorMessage Optional output for creation failures.
   * @return True when the directory exists or was created successfully.
   */
  bool ensure_directory_exists(const std::string &directoryPath, std::string *errorMessage = nullptr);

  /**
   * @brief Try to read the size of a regular file.
   *
   * @param path File path to inspect.
   * @param sizeBytes Optional output for the file size in bytes.
   * @return True when the path exists and is a regular file.
   */
  bool try_get_file_size(std::string_view path, std::uint64_t *sizeBytes = nullptr);

  /**
   * @brief Return whether a path begins with the requested prefix.
   *
   * @param path Full path to inspect.
   * @param prefix Prefix to compare against.
   * @return True when the path starts with the prefix.
   */
  bool path_has_prefix(const std::string &path, const std::string &prefix);

}  // namespace platform
