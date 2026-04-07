// class header include
#include "src/platform/filesystem_utils.h"

// standard includes
#include <cctype>
#include <cerrno>
#include <cstring>

// platform includes
#if defined(_WIN32) || defined(NXDK)
  #include <windows.h>
extern "C" {
  #include <direct.h>
}
#else
  #include <sys/stat.h>
  #include <sys/types.h>
#endif

namespace {

  bool is_drive_root_path(const std::string &path) {
#if defined(_WIN32) || defined(NXDK)
    return path.size() <= 3 && path.size() >= 2 && path[1] == ':';
#else
    (void) path;
    return false;
#endif
  }

  bool is_root_path(const std::string &path) {
#if defined(_WIN32) || defined(NXDK)
    return is_drive_root_path(path);
#else
    return path == "/";
#endif
  }

  std::string normalize_directory_component(std::string path) {
    while (path.size() > 1 && platform::is_path_separator(path.back())) {
      if (is_root_path(path)) {
        break;
      }
      path.pop_back();
    }
    return path;
  }

  bool create_directory_if_missing(const std::string &path, std::string *errorMessage) {
#if defined(_WIN32) || defined(NXDK)
    if (_mkdir(path.c_str()) == 0 || errno == EEXIST) {
      return true;
    }
#else
    if (mkdir(path.c_str(), 0777) == 0 || errno == EEXIST) {
      return true;
    }
#endif

    if (errorMessage != nullptr) {
      *errorMessage = "Failed to create directory '" + path + "': " + std::strerror(errno);
    }
    return false;
  }

  bool path_char_equal(char left, char right) {
#if defined(_WIN32) || defined(NXDK)
    return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
#else
    return left == right;
#endif
  }

}  // namespace

namespace platform {

  char preferred_path_separator() {
#if defined(_WIN32) || defined(NXDK)
    return '\\';
#else
    return '/';
#endif
  }

  bool is_path_separator(char character) {
    return character == '\\' || character == '/';
  }

  std::string join_path(const std::string &left, const std::string &right) {
    if (left.empty()) {
      return right;
    }
    if (right.empty()) {
      return left;
    }
    if (is_path_separator(left.back())) {
      return left + right;
    }
    return left + preferred_path_separator() + right;
  }

  std::string parent_directory(const std::string &filePath) {
    const std::size_t separatorIndex = filePath.find_last_of("\\/");
    if (separatorIndex == std::string::npos) {
      return {};
    }
    return filePath.substr(0, separatorIndex);
  }

  bool ensure_directory_exists(const std::string &directoryPath, std::string *errorMessage) {
    if (directoryPath.empty()) {
      return true;
    }

    std::string partialPath;
    std::size_t startIndex = 0;
#if defined(_WIN32) || defined(NXDK)
    if (directoryPath.size() >= 2 && directoryPath[1] == ':') {
      partialPath = directoryPath.substr(0, 2);
      startIndex = 2;
    }
#else
    if (is_path_separator(directoryPath.front())) {
      partialPath = "/";
      startIndex = 1;
    }
#endif

    for (std::size_t index = startIndex; index < directoryPath.size(); ++index) {
      partialPath.push_back(directoryPath[index]);
      const bool atSeparator = is_path_separator(directoryPath[index]);
      const bool atPathEnd = index + 1 == directoryPath.size();
      if (!atSeparator && !atPathEnd) {
        continue;
      }

      const std::string normalizedPath = normalize_directory_component(partialPath);
      if (normalizedPath.empty() || is_root_path(normalizedPath)) {
        continue;
      }

      if (!create_directory_if_missing(normalizedPath, errorMessage)) {
        return false;
      }
    }

    return true;
  }

  bool try_get_file_size(const std::string &path, std::uint64_t *sizeBytes) {
#if defined(_WIN32) || defined(NXDK)
    WIN32_FILE_ATTRIBUTE_DATA fileData {};
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fileData)) {
      return false;
    }
    if ((fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
      return false;
    }

    if (sizeBytes != nullptr) {
      ULARGE_INTEGER sizeValue {};
      sizeValue.HighPart = fileData.nFileSizeHigh;
      sizeValue.LowPart = fileData.nFileSizeLow;
      *sizeBytes = sizeValue.QuadPart;
    }
    return true;
#else
    struct stat status {};
    if (stat(path.c_str(), &status) != 0 || !S_ISREG(status.st_mode)) {
      return false;
    }

    if (sizeBytes != nullptr) {
      *sizeBytes = static_cast<std::uint64_t>(status.st_size);
    }
    return true;
#endif
  }

  bool path_has_prefix(const std::string &path, const std::string &prefix) {
    if (prefix.empty() || path.size() < prefix.size()) {
      return false;
    }

    for (std::size_t index = 0; index < prefix.size(); ++index) {
      if (!path_char_equal(path[index], prefix[index])) {
        return false;
      }
    }
    return true;
  }

}  // namespace platform
