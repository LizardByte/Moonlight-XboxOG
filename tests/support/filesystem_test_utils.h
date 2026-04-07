#pragma once

// standard includes
#include <cstdio>
#include <filesystem>
#include <string>

namespace test_support {

  inline std::string join_path(const std::string &left, const std::string &right) {
    return (std::filesystem::path(left) / right).make_preferred().string();
  }

  inline void remove_if_present(const std::string &path) {
    std::remove(path.c_str());
  }

  inline void remove_directory_if_present(const std::string &path) {
    std::error_code error;
    std::filesystem::remove(path, error);
  }

  inline void remove_tree_if_present(const std::string &path) {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }

  inline bool create_directory(const std::string &path) {
    std::error_code error;
    return std::filesystem::create_directory(path, error) || std::filesystem::is_directory(path, error);
  }

}  // namespace test_support
