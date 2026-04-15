/**
 * @file src/startup/host_storage.cpp
 * @brief Implements saved host persistence.
 */
// class header include
#include "src/startup/host_storage.h"

// standard includes
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// local includes
#include "src/platform/filesystem_utils.h"
#include "src/startup/storage_paths.h"

namespace {

  std::string read_all_text(FILE *file) {
    std::string content;
    std::vector<char> buffer(4096);

    while (true) {
      const std::size_t bytesRead = std::fread(buffer.data(), 1, buffer.size(), file);
      if (bytesRead > 0) {
        content.append(buffer.data(), bytesRead);
      }

      if (bytesRead < buffer.size()) {
        break;
      }
    }

    return content;
  }

}  // namespace

namespace startup {

  std::string default_host_storage_path() {
    return default_storage_path("moonlight-hosts.tsv");
  }

  LoadSavedHostsResult load_saved_hosts(const std::string &filePath) {
    LoadSavedHostsResult result {{}, {}, false};

    FILE *file = std::fopen(filePath.c_str(), "rb");
    if (file == nullptr) {
      if (errno != ENOENT) {
        result.warnings.push_back("Failed to open saved hosts file '" + filePath + "': " + std::strerror(errno));
      }
      return result;
    }

    result.fileFound = true;
    const std::string serializedHosts = read_all_text(file);
    if (std::ferror(file) != 0) {
      result.warnings.push_back("Failed while reading saved hosts file '" + filePath + "': " + std::strerror(errno));
    }
    std::fclose(file);

    const app::ParseHostRecordsResult parsedHosts = app::parse_host_records(serializedHosts);
    result.hosts = parsedHosts.records;
    result.warnings = parsedHosts.errors;
    return result;
  }

  SaveSavedHostsResult save_saved_hosts(const std::vector<app::HostRecord> &hosts, const std::string &filePath) {
    if (std::string errorMessage; !platform::ensure_directory_exists(platform::parent_directory(filePath), &errorMessage)) {
      return {false, errorMessage};
    }

    FILE *file = std::fopen(filePath.c_str(), "wb");
    if (file == nullptr) {
      return {false, "Failed to open saved hosts file '" + filePath + "' for writing: " + std::strerror(errno)};
    }

    const std::string serializedHosts = app::serialize_host_records(hosts);
    if (const std::size_t bytesWritten = std::fwrite(serializedHosts.data(), 1, serializedHosts.size(), file); bytesWritten != serializedHosts.size()) {
      const std::string writeErrorMessage = "Failed to write saved hosts file '" + filePath + "': " + std::strerror(errno);
      std::fclose(file);
      return {false, writeErrorMessage};
    }

    if (std::fclose(file) != 0) {
      return {false, "Failed to finalize saved hosts file '" + filePath + "': " + std::strerror(errno)};
    }

    return {true, {}};
  }

}  // namespace startup
