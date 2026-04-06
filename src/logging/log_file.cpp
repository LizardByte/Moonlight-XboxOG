// class header include
#include "src/logging/log_file.h"

// standard includes
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>

extern "C" {
#include <direct.h>
}

// local includes
#include "src/startup/host_storage.h"

namespace {

  std::string normalize_directory_component(std::string path) {
    while (path.size() > 3 && (path.back() == '\\' || path.back() == '/')) {
      path.pop_back();
    }
    return path;
  }

  bool is_drive_root_path(const std::string &path) {
    return path.size() <= 3 && path.size() >= 2 && path[1] == ':';
  }

  bool ensure_directory_exists(const std::string &directoryPath, std::string *errorMessage) {
    if (directoryPath.empty()) {
      return true;
    }

    std::string partialPath;
    std::size_t startIndex = 0;
    if (directoryPath.size() >= 2 && directoryPath[1] == ':') {
      partialPath = directoryPath.substr(0, 2);
      startIndex = 2;
    }

    for (std::size_t index = startIndex; index < directoryPath.size(); ++index) {
      partialPath.push_back(directoryPath[index]);
      const bool atSeparator = directoryPath[index] == '\\' || directoryPath[index] == '/';
      const bool atPathEnd = index + 1 == directoryPath.size();
      if (!atSeparator && !atPathEnd) {
        continue;
      }

      if (is_drive_root_path(partialPath)) {
        continue;
      }

      const std::string normalizedPath = normalize_directory_component(partialPath);
      if (normalizedPath.empty()) {
        continue;
      }

      if (_mkdir(normalizedPath.c_str()) != 0 && errno != EEXIST) {
        if (errorMessage != nullptr) {
          *errorMessage = "Failed to create directory '" + normalizedPath + "': " + std::strerror(errno);
        }
        return false;
      }
    }

    return true;
  }

  std::string parent_directory(const std::string &filePath) {
    const std::size_t separatorIndex = filePath.find_last_of("\\/");
    if (separatorIndex == std::string::npos) {
      return {};
    }

    return filePath.substr(0, separatorIndex);
  }

  std::string persisted_log_line(const logging::LogEntry &entry) {
    return std::string("[") + logging::format_timestamp(entry.timestamp) + "] " + logging::format_entry(entry);
  }

}  // namespace

namespace logging {

  std::string default_log_file_path() {
    const std::string hostStoragePath = startup::default_host_storage_path();
    const std::string directoryPath = parent_directory(hostStoragePath);
    if (directoryPath.empty()) {
      return "moonlight.log";
    }

    return directoryPath + "\\moonlight.log";
  }

  bool reset_log_file(const std::string &filePath, std::string *errorMessage) {
    if (!ensure_directory_exists(parent_directory(filePath), errorMessage)) {
      return false;
    }

    FILE *file = std::fopen(filePath.c_str(), "wb");
    if (file == nullptr) {
      if (errorMessage != nullptr) {
        *errorMessage = "Failed to reset log file '" + filePath + "': " + std::strerror(errno);
      }
      return false;
    }

    if (std::fclose(file) != 0) {
      if (errorMessage != nullptr) {
        *errorMessage = "Failed to finalize log file '" + filePath + "': " + std::strerror(errno);
      }
      return false;
    }

    return true;
  }

  bool append_log_file_entry(const LogEntry &entry, const std::string &filePath, std::string *errorMessage) {
    if (!ensure_directory_exists(parent_directory(filePath), errorMessage)) {
      return false;
    }

    FILE *file = std::fopen(filePath.c_str(), "ab");
    if (file == nullptr) {
      if (errorMessage != nullptr) {
        *errorMessage = "Failed to open log file '" + filePath + "' for appending: " + std::strerror(errno);
      }
      return false;
    }

    const std::string line = persisted_log_line(entry) + "\r\n";
    const std::size_t bytesWritten = std::fwrite(line.data(), 1, line.size(), file);
    if (bytesWritten != line.size()) {
      if (errorMessage != nullptr) {
        *errorMessage = "Failed to append to log file '" + filePath + "': " + std::strerror(errno);
      }
      std::fclose(file);
      return false;
    }

    if (std::fclose(file) != 0) {
      if (errorMessage != nullptr) {
        *errorMessage = "Failed to finalize log file '" + filePath + "': " + std::strerror(errno);
      }
      return false;
    }

    return true;
  }

  LoadLogFileResult load_log_file(const std::string &filePath, std::size_t maxLines) {
    LoadLogFileResult result {};
    result.filePath = filePath;

    FILE *file = std::fopen(filePath.c_str(), "rb");
    if (file == nullptr) {
      if (errno != ENOENT) {
        result.errorMessage = "Failed to open log file '" + filePath + "': " + std::strerror(errno);
      }
      return result;
    }

    result.fileFound = true;
    std::deque<std::string> bufferedLines;
    auto append_line = [&](std::string line) {
      while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
      }

      if (maxLines > 0U && bufferedLines.size() == maxLines) {
        bufferedLines.pop_front();
      }
      bufferedLines.push_back(std::move(line));
    };

    char buffer[1024] = {};
    std::string pendingLine;
    while (std::fgets(buffer, static_cast<int>(sizeof(buffer)), file) != nullptr) {
      pendingLine += buffer;
      const std::size_t pendingLength = pendingLine.size();
      if (pendingLength == 0U) {
        continue;
      }

      if (pendingLine.back() == '\n' || pendingLine.back() == '\r') {
        append_line(std::move(pendingLine));
        pendingLine.clear();
      }
    }

    if (!pendingLine.empty()) {
      append_line(std::move(pendingLine));
    }

    if (std::ferror(file) != 0) {
      result.errorMessage = "Failed while reading log file '" + filePath + "': " + std::strerror(errno);
    }
    std::fclose(file);

    result.lines.assign(bufferedLines.begin(), bufferedLines.end());
    return result;
  }

}  // namespace logging
