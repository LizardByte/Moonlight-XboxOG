// class header include
#include "src/logging/log_file.h"

// standard includes
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>

// local includes
#include "src/platform/filesystem_utils.h"
#include "src/startup/storage_paths.h"

namespace {

  std::string persisted_log_line(const logging::LogEntry &entry) {
    return std::string("[") + logging::format_timestamp(entry.timestamp) + "] " + logging::format_entry(entry);
  }

}  // namespace

namespace logging {

  std::string default_log_file_path() {
    return startup::default_storage_path("moonlight.log");
  }

  bool reset_log_file(const std::string &filePath, std::string *errorMessage) {
    if (!platform::ensure_directory_exists(platform::parent_directory(filePath), errorMessage)) {
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
    if (!platform::ensure_directory_exists(platform::parent_directory(filePath), errorMessage)) {
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
    if (const std::size_t bytesWritten = std::fwrite(line.data(), 1, line.size(), file); bytesWritten != line.size()) {
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

  RuntimeLogFileSink::RuntimeLogFileSink(std::string filePath):
      filePath_(std::move(filePath)) {}

  const std::string &RuntimeLogFileSink::file_path() const {
    return filePath_;
  }

  bool RuntimeLogFileSink::reset(std::string *errorMessage) const {
    return reset_log_file(filePath_, errorMessage);
  }

  bool RuntimeLogFileSink::consume(const LogEntry &entry, std::string *errorMessage) const {
    return append_log_file_entry(entry, filePath_, errorMessage);
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

    std::array<char, 1024> buffer {};
    std::string pendingLine;
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), file) != nullptr) {
      pendingLine += buffer.data();
      if (const std::size_t pendingLength = pendingLine.size(); pendingLength == 0U) {
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
