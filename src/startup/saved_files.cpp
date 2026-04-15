/**
 * @file src/startup/saved_files.cpp
 * @brief Implements saved file loading and cleanup helpers.
 */
// class header include
#include "src/startup/saved_files.h"

// standard includes
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// platform includes
#if defined(_WIN32) || defined(NXDK)
  #include <windows.h>  // NOSONAR(cpp:S3806) nxdk requires lowercase header names
#else
  #include <dirent.h>
  #include <sys/stat.h>
  #include <sys/types.h>
#endif

// local includes
#include "src/app/settings_storage.h"
#include "src/logging/log_file.h"
#include "src/platform/error_utils.h"
#include "src/platform/filesystem_utils.h"
#include "src/startup/client_identity_storage.h"
#include "src/startup/cover_art_cache.h"
#include "src/startup/host_storage.h"

namespace {

  constexpr const char *PAIRING_UNIQUE_ID_FILE_NAME = "uniqueid.dat";
  constexpr const char *PAIRING_CERTIFICATE_FILE_NAME = "client.pem";
  constexpr const char *PAIRING_PRIVATE_KEY_FILE_NAME = "key.pem";

  struct ResolvedSavedFileCatalogConfig {
    std::string hostStoragePath;
    std::string settingsFilePath;
    std::string logFilePath;
    std::string pairingDirectory;
    std::string coverArtCacheRoot;
  };

  using platform::append_error;
  using platform::file_name_from_path;
  using platform::join_path;
  using platform::path_has_prefix;
  using platform::try_get_file_size;

  std::string relative_path_from_root(const std::string &rootPath, const std::string &path) {
    if (!path_has_prefix(path, rootPath)) {
      return file_name_from_path(path);
    }

    std::size_t offset = rootPath.size();
    while (offset < path.size() && (path[offset] == '\\' || path[offset] == '/')) {
      ++offset;
    }
    return offset >= path.size() ? file_name_from_path(path) : path.substr(offset);
  }

  ResolvedSavedFileCatalogConfig resolve_config(const startup::SavedFileCatalogConfig &config) {
    return {
      config.hostStoragePath.empty() ? startup::default_host_storage_path() : config.hostStoragePath,
      config.settingsFilePath.empty() ? app::default_settings_path() : config.settingsFilePath,
      config.logFilePath.empty() ? logging::default_log_file_path() : config.logFilePath,
      config.pairingDirectory.empty() ? startup::default_client_identity_directory() : config.pairingDirectory,
      config.coverArtCacheRoot.empty() ? startup::default_cover_art_cache_root() : config.coverArtCacheRoot,
    };
  }

  void add_file_if_present(
    std::vector<startup::SavedFileEntry> *files,
    std::unordered_map<std::string, bool> *seenPaths,
    const std::string &path,
    const std::string &displayName
  ) {
    if (files == nullptr || seenPaths == nullptr || path.empty()) {
      return;
    }

    if (seenPaths->find(path) != seenPaths->end()) {
      return;
    }

    std::uint64_t sizeBytes = 0;
    if (!try_get_file_size(path, &sizeBytes)) {
      return;
    }

    seenPaths->emplace(path, true);
    files->push_back({path, displayName, sizeBytes});
  }

  void append_directory_files(
    std::vector<startup::SavedFileEntry> *files,
    std::unordered_map<std::string, bool> *seenPaths,
    std::vector<std::string> *warnings,
    const std::string &rootPath,
    const std::string &displayPrefix
  ) {
    if (files == nullptr || seenPaths == nullptr || rootPath.empty()) {
      return;
    }

#if defined(_WIN32) || defined(NXDK)
    WIN32_FIND_DATAA findData {};
    const std::string searchPattern = join_path(rootPath, "*");
    HANDLE handle = FindFirstFileA(searchPattern.c_str(), &findData);
    if (handle == INVALID_HANDLE_VALUE) {
      if (const DWORD errorCode = GetLastError(); errorCode != ERROR_FILE_NOT_FOUND && errorCode != ERROR_PATH_NOT_FOUND && warnings != nullptr) {
        warnings->push_back("Failed to enumerate saved files in '" + rootPath + "': error " + std::to_string(errorCode));
      }
      return;
    }

    do {
      const std::string entryName = findData.cFileName;
      if (entryName == "." || entryName == "..") {
        continue;
      }

      const std::string entryPath = join_path(rootPath, entryName);
      if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
        append_directory_files(files, seenPaths, warnings, entryPath, displayPrefix);
        continue;
      }

      const std::string relativePath = relative_path_from_root(rootPath, entryPath);
      const std::string displayName = displayPrefix.empty() ? relativePath : join_path(displayPrefix, relativePath);
      add_file_if_present(files, seenPaths, entryPath, displayName);
    } while (FindNextFileA(handle, &findData) != 0);

    const DWORD lastError = GetLastError();
    FindClose(handle);
    if (lastError != ERROR_NO_MORE_FILES && warnings != nullptr) {
      warnings->push_back("Stopped enumerating saved files in '" + rootPath + "' early: error " + std::to_string(lastError));
    }
#else
    DIR *directory = opendir(rootPath.c_str());
    if (directory == nullptr) {
      if (errno != ENOENT && errno != ENOTDIR && warnings != nullptr) {
        warnings->push_back("Failed to enumerate saved files in '" + rootPath + "': " + std::strerror(errno));
      }
      return;
    }

    errno = 0;
    while (const dirent *entry = readdir(directory)) {
      const std::string entryName = entry->d_name;
      if (entryName == "." || entryName == "..") {
        continue;
      }

      const std::string entryPath = join_path(rootPath, entryName);
      struct stat status {};
      if (stat(entryPath.c_str(), &status) != 0) {
        if (warnings != nullptr) {
          warnings->push_back("Failed to inspect saved file entry '" + entryPath + "': " + std::strerror(errno));
        }
        continue;
      }

      if (S_ISDIR(status.st_mode)) {
        append_directory_files(files, seenPaths, warnings, entryPath, displayPrefix);
        continue;
      }
      if (!S_ISREG(status.st_mode)) {
        continue;
      }

      const std::string relativePath = relative_path_from_root(rootPath, entryPath);
      const std::string displayName = displayPrefix.empty() ? relativePath : join_path(displayPrefix, relativePath);
      add_file_if_present(files, seenPaths, entryPath, displayName);
    }

    const int readError = errno;
    closedir(directory);
    if (readError != 0 && warnings != nullptr) {
      warnings->push_back("Stopped enumerating saved files in '" + rootPath + "' early: " + std::strerror(readError));
    }
#endif
  }

  bool path_is_managed_saved_file(const std::string &path, const ResolvedSavedFileCatalogConfig &config) {
    if (path == config.hostStoragePath || path == config.settingsFilePath || path == config.logFilePath) {
      return true;
    }

    if (const std::vector<std::string> pairingFiles = {
          join_path(config.pairingDirectory, PAIRING_UNIQUE_ID_FILE_NAME),
          join_path(config.pairingDirectory, PAIRING_CERTIFICATE_FILE_NAME),
          join_path(config.pairingDirectory, PAIRING_PRIVATE_KEY_FILE_NAME),
        };
        std::find(pairingFiles.begin(), pairingFiles.end(), path) != pairingFiles.end()) {
      return true;
    }

    if (!config.coverArtCacheRoot.empty() && path_has_prefix(path, config.coverArtCacheRoot)) {
      const std::size_t rootLength = config.coverArtCacheRoot.size();
      if (path.size() == rootLength) {
        return false;
      }
      return path[rootLength] == '\\' || path[rootLength] == '/';
    }

    return false;
  }

}  // namespace

namespace startup {

  ListSavedFilesResult list_saved_files(const SavedFileCatalogConfig &config) {
    const ResolvedSavedFileCatalogConfig resolvedConfig = resolve_config(config);
    ListSavedFilesResult result {};
    std::unordered_map<std::string, bool> seenPaths;

    add_file_if_present(&result.files, &seenPaths, resolvedConfig.hostStoragePath, file_name_from_path(resolvedConfig.hostStoragePath));
    add_file_if_present(&result.files, &seenPaths, resolvedConfig.settingsFilePath, file_name_from_path(resolvedConfig.settingsFilePath));
    add_file_if_present(&result.files, &seenPaths, resolvedConfig.logFilePath, file_name_from_path(resolvedConfig.logFilePath));
    add_file_if_present(&result.files, &seenPaths, join_path(resolvedConfig.pairingDirectory, PAIRING_UNIQUE_ID_FILE_NAME), join_path("pairing", PAIRING_UNIQUE_ID_FILE_NAME));
    add_file_if_present(&result.files, &seenPaths, join_path(resolvedConfig.pairingDirectory, PAIRING_CERTIFICATE_FILE_NAME), join_path("pairing", PAIRING_CERTIFICATE_FILE_NAME));
    add_file_if_present(&result.files, &seenPaths, join_path(resolvedConfig.pairingDirectory, PAIRING_PRIVATE_KEY_FILE_NAME), join_path("pairing", PAIRING_PRIVATE_KEY_FILE_NAME));
    append_directory_files(&result.files, &seenPaths, &result.warnings, resolvedConfig.coverArtCacheRoot, "cover-art-cache");

    std::sort(result.files.begin(), result.files.end(), [](const SavedFileEntry &left, const SavedFileEntry &right) {
      return left.displayName < right.displayName;
    });
    return result;
  }

  bool delete_saved_file(const std::string &path, std::string *errorMessage, const SavedFileCatalogConfig &config) {
    const ResolvedSavedFileCatalogConfig resolvedConfig = resolve_config(config);
    if (path.empty()) {
      return append_error(errorMessage, "Saved file deletion requires a valid path");
    }
    if (!path_is_managed_saved_file(path, resolvedConfig)) {
      return append_error(errorMessage, "Refused to delete a file outside the Moonlight-managed storage set");
    }

    if (std::remove(path.c_str()) == 0) {
      return true;
    }
    if (errno == ENOENT) {
      return true;
    }
    return append_error(errorMessage, "Failed to delete saved file '" + path + "': " + std::strerror(errno));
  }

  bool delete_all_saved_files(std::string *errorMessage, const SavedFileCatalogConfig &config) {
    const ListSavedFilesResult savedFiles = list_saved_files(config);
    if (!savedFiles.warnings.empty()) {
      return append_error(errorMessage, savedFiles.warnings.front());
    }

    for (const SavedFileEntry &savedFile : savedFiles.files) {
      std::string deleteError;
      if (!delete_saved_file(savedFile.path, &deleteError, config)) {
        return append_error(errorMessage, deleteError);
      }
    }

    return true;
  }

}  // namespace startup
