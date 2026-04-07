// class header include
#include "src/startup/host_storage.h"

// standard includes
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// nxdk includes
#if defined(__has_include)
  #if __has_include(<nxdk/xbe.h>)
    #include <nxdk/xbe.h>
    #include <winnt.h>
    #define MOONLIGHT_HAS_NXDK_XBE 1
  #endif
  #if __has_include(<nxdk/mount.h>)
    #include <nxdk/mount.h>
    #define MOONLIGHT_HAS_NXDK_MOUNT 1
  #endif
#endif

// local includes
#include "src/platform/filesystem_utils.h"

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

  std::string title_scoped_storage_root() {
#ifdef MOONLIGHT_HAS_NXDK_XBE
  #ifdef MOONLIGHT_HAS_NXDK_MOUNT
    if (!nxIsDriveMounted('E') && !nxMountDrive('E', "\\Device\\Harddisk0\\Partition1\\")) {
      return {};
    }
  #endif

    char titleIdBuffer[9] = {};
    std::snprintf(titleIdBuffer, sizeof(titleIdBuffer), "%08X", CURRENT_XBE_HEADER->CertificateHeader->TitleID);
    return std::string("E:\\UDATA\\") + titleIdBuffer + "\\";
#else
    return {};
#endif
  }

}  // namespace

namespace startup {

  std::string default_host_storage_path() {
    const std::string titleScopedRoot = title_scoped_storage_root();
    if (!titleScopedRoot.empty()) {
      return titleScopedRoot + "moonlight-hosts.tsv";
    }

    return {"moonlight-hosts.tsv"};
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
    std::string errorMessage;
    if (!platform::ensure_directory_exists(platform::parent_directory(filePath), &errorMessage)) {
      return {false, errorMessage};
    }

    FILE *file = std::fopen(filePath.c_str(), "wb");
    if (file == nullptr) {
      return {false, "Failed to open saved hosts file '" + filePath + "' for writing: " + std::strerror(errno)};
    }

    const std::string serializedHosts = app::serialize_host_records(hosts);
    const std::size_t bytesWritten = std::fwrite(serializedHosts.data(), 1, serializedHosts.size(), file);
    if (bytesWritten != serializedHosts.size()) {
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
