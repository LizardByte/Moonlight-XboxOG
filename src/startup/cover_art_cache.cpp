// class header include
#include "src/startup/cover_art_cache.h"

// standard includes
#include <cerrno>
#include <cstdint>
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

  bool append_error(std::string *errorMessage, std::string message) {
    if (errorMessage != nullptr) {
      *errorMessage = std::move(message);
    }
    return false;
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

  std::string cover_art_cache_path(std::string_view cacheKey, const std::string &cacheRoot) {
    return platform::join_path(cacheRoot, std::string(cacheKey) + ".bin");
  }

  uint64_t fnv1a_64(std::string_view text) {
    uint64_t hash = 1469598103934665603ULL;
    for (char character : text) {
      hash ^= static_cast<unsigned char>(character);
      hash *= 1099511628211ULL;
    }
    return hash;
  }

  std::string hex64(uint64_t value) {
    static constexpr char HEX_DIGITS[] = "0123456789abcdef";

    std::string text(16, '0');
    for (int index = 15; index >= 0; --index) {
      text[static_cast<std::size_t>(index)] = HEX_DIGITS[value & 0x0FULL];
      value >>= 4U;
    }
    return text;
  }

  std::vector<unsigned char> read_all_bytes(FILE *file) {
    std::vector<unsigned char> bytes;
    std::vector<unsigned char> buffer(4096);

    while (true) {
      const std::size_t bytesRead = std::fread(buffer.data(), 1, buffer.size(), file);
      if (bytesRead > 0) {
        bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(bytesRead));
      }

      if (bytesRead < buffer.size()) {
        break;
      }
    }

    return bytes;
  }

}  // namespace

namespace startup {

  std::string default_cover_art_cache_root() {
    const std::string titleScopedRoot = title_scoped_storage_root();
    if (!titleScopedRoot.empty()) {
      return titleScopedRoot + "cover-art-cache";
    }

    return {"moonlight-cover-art-cache"};
  }

  std::string build_cover_art_cache_key(std::string_view hostUuid, std::string_view hostAddress, int appId) {
    const std::string_view hostIdentity = hostUuid.empty() ? hostAddress : hostUuid;
    return hex64(fnv1a_64(std::string(hostIdentity) + "|" + std::to_string(appId))) + "-" + std::to_string(appId);
  }

  bool cover_art_exists(std::string_view cacheKey, const std::string &cacheRoot) {
    FILE *file = std::fopen(cover_art_cache_path(cacheKey, cacheRoot).c_str(), "rb");
    if (file == nullptr) {
      return false;
    }

    std::fclose(file);
    return true;
  }

  bool delete_cover_art(std::string_view cacheKey, std::string *errorMessage, const std::string &cacheRoot) {
    if (cacheKey.empty()) {
      return true;
    }

    const std::string path = cover_art_cache_path(cacheKey, cacheRoot);
    if (std::remove(path.c_str()) == 0 || errno == ENOENT) {
      return true;
    }

    return append_error(errorMessage, "Failed to delete cached cover art '" + path + "': " + std::strerror(errno));
  }

  LoadCoverArtResult load_cover_art(std::string_view cacheKey, const std::string &cacheRoot) {
    LoadCoverArtResult result {};
    FILE *file = std::fopen(cover_art_cache_path(cacheKey, cacheRoot).c_str(), "rb");
    if (file == nullptr) {
      if (errno != ENOENT) {
        result.errorMessage = std::strerror(errno);
      }
      return result;
    }

    result.fileFound = true;
    result.bytes = read_all_bytes(file);
    if (std::ferror(file) != 0) {
      result.errorMessage = std::strerror(errno);
    }
    std::fclose(file);
    return result;
  }

  SaveCoverArtResult save_cover_art(std::string_view cacheKey, const std::vector<unsigned char> &bytes, const std::string &cacheRoot) {
    std::string errorMessage;
    if (!platform::ensure_directory_exists(cacheRoot, &errorMessage)) {
      return {false, errorMessage};
    }
    if (!platform::ensure_directory_exists(platform::parent_directory(cover_art_cache_path(cacheKey, cacheRoot)), &errorMessage)) {
      return {false, errorMessage};
    }

    FILE *file = std::fopen(cover_art_cache_path(cacheKey, cacheRoot).c_str(), "wb");
    if (file == nullptr) {
      return {false, "Failed to open cover-art cache entry for writing: " + std::string(std::strerror(errno))};
    }

    const std::size_t bytesWritten = std::fwrite(bytes.data(), 1, bytes.size(), file);
    if (bytesWritten != bytes.size()) {
      const std::string writeError = std::strerror(errno);
      std::fclose(file);
      return {false, "Failed to write the cover-art cache entry: " + writeError};
    }

    if (std::fclose(file) != 0) {
      return {false, "Failed to finalize the cover-art cache entry: " + std::string(std::strerror(errno))};
    }

    return {true, {}};
  }

}  // namespace startup
