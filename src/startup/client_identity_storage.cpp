// class header include
#include "src/startup/client_identity_storage.h"

// standard includes
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include <direct.h>
}

// local includes
#include "src/startup/host_storage.h"

namespace {

  constexpr const char *UNIQUE_ID_FILE_NAME = "uniqueid.dat";
  constexpr const char *CERTIFICATE_FILE_NAME = "client.pem";
  constexpr const char *PRIVATE_KEY_FILE_NAME = "key.pem";

  struct ReadFileTextResult {
    std::string content;
    int errorCode;
  };

  std::string parent_directory(const std::string &filePath) {
    const std::size_t separatorIndex = filePath.find_last_of("\\/");
    if (separatorIndex == std::string::npos) {
      return {};
    }

    return filePath.substr(0, separatorIndex);
  }

  std::string join_path(const std::string &left, const std::string &right) {
    if (left.empty()) {
      return right;
    }
    if (left.back() == '\\' || left.back() == '/') {
      return left + right;
    }
    return left + "\\" + right;
  }

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

  ReadFileTextResult read_file_text(const std::string &filePath, std::string *errorMessage) {
    FILE *file = std::fopen(filePath.c_str(), "rb");
    if (file == nullptr) {
      if (errorMessage != nullptr) {
        *errorMessage = std::strerror(errno);
      }
      return {{}, errno};
    }

    std::vector<char> buffer(4096);
    std::string content;
    while (true) {
      const std::size_t bytesRead = std::fread(buffer.data(), 1, buffer.size(), file);
      if (bytesRead > 0) {
        content.append(buffer.data(), bytesRead);
      }
      if (bytesRead < buffer.size()) {
        break;
      }
    }

    if (std::fclose(file) != 0 && errorMessage != nullptr) {
      *errorMessage = std::strerror(errno);
    }
    return {std::move(content), 0};
  }

  bool write_file_text(const std::string &filePath, const std::string &content, std::string *errorMessage) {
    FILE *file = std::fopen(filePath.c_str(), "wb");
    if (file == nullptr) {
      if (errorMessage != nullptr) {
        *errorMessage = std::strerror(errno);
      }
      return false;
    }

    const std::size_t bytesWritten = std::fwrite(content.data(), 1, content.size(), file);
    if (bytesWritten != content.size()) {
      if (errorMessage != nullptr) {
        *errorMessage = std::strerror(errno);
      }
      std::fclose(file);
      return false;
    }

    if (std::fclose(file) != 0) {
      if (errorMessage != nullptr) {
        *errorMessage = std::strerror(errno);
      }
      return false;
    }

    return true;
  }

}  // namespace

namespace startup {

  std::string default_client_identity_directory() {
    const std::string hostStorageDirectory = parent_directory(default_host_storage_path());
    if (hostStorageDirectory.empty()) {
      return "pairing";
    }

    return join_path(hostStorageDirectory, "pairing");
  }

  LoadClientIdentityResult load_client_identity(const std::string &directoryPath) {
    LoadClientIdentityResult result {{}, {}, false};

    const std::string uniqueIdPath = join_path(directoryPath, UNIQUE_ID_FILE_NAME);
    const std::string certificatePath = join_path(directoryPath, CERTIFICATE_FILE_NAME);
    const std::string privateKeyPath = join_path(directoryPath, PRIVATE_KEY_FILE_NAME);

    std::string uniqueIdError;
    const ReadFileTextResult uniqueIdRead = read_file_text(uniqueIdPath, &uniqueIdError);
    std::string uniqueId = uniqueIdRead.content;
    if (uniqueId.empty()) {
      if (uniqueIdRead.errorCode != ENOENT) {
        result.warnings.push_back("Failed to load pairing unique ID from '" + uniqueIdPath + "': " + uniqueIdError);
      }
      return result;
    }

    std::string certificateError;
    const ReadFileTextResult certificateRead = read_file_text(certificatePath, &certificateError);
    std::string certificatePem = certificateRead.content;
    if (certificatePem.empty()) {
      result.warnings.push_back("Failed to load pairing certificate from '" + certificatePath + "': " + certificateError);
      return result;
    }

    std::string privateKeyError;
    const ReadFileTextResult privateKeyRead = read_file_text(privateKeyPath, &privateKeyError);
    std::string privateKeyPem = privateKeyRead.content;
    if (privateKeyPem.empty()) {
      result.warnings.push_back("Failed to load pairing private key from '" + privateKeyPath + "': " + privateKeyError);
      return result;
    }

    while (!uniqueId.empty() && (uniqueId.back() == '\r' || uniqueId.back() == '\n')) {
      uniqueId.pop_back();
    }

    result.identity = {std::move(uniqueId), std::move(certificatePem), std::move(privateKeyPem)};
    result.fileFound = true;
    return result;
  }

  SaveClientIdentityResult save_client_identity(const network::PairingIdentity &identity, const std::string &directoryPath) {
    std::string errorMessage;
    if (!ensure_directory_exists(directoryPath, &errorMessage)) {
      return {false, errorMessage};
    }

    const std::string uniqueIdPath = join_path(directoryPath, UNIQUE_ID_FILE_NAME);
    if (!write_file_text(uniqueIdPath, identity.uniqueId, &errorMessage)) {
      return {false, "Failed to save pairing unique ID to '" + uniqueIdPath + "': " + errorMessage};
    }

    const std::string certificatePath = join_path(directoryPath, CERTIFICATE_FILE_NAME);
    if (!write_file_text(certificatePath, identity.certificatePem, &errorMessage)) {
      return {false, "Failed to save pairing certificate to '" + certificatePath + "': " + errorMessage};
    }

    const std::string privateKeyPath = join_path(directoryPath, PRIVATE_KEY_FILE_NAME);
    if (!write_file_text(privateKeyPath, identity.privateKeyPem, &errorMessage)) {
      return {false, "Failed to save pairing private key to '" + privateKeyPath + "': " + errorMessage};
    }

    return {true, {}};
  }

}  // namespace startup
