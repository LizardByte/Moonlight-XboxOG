// class header include
#include "src/startup/client_identity_storage.h"

// standard includes
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// local includes
#include "src/platform/filesystem_utils.h"
#include "src/startup/host_storage.h"

namespace {

  bool append_error(std::string *errorMessage, std::string message) {
    if (errorMessage != nullptr) {
      *errorMessage = std::move(message);
    }
    return false;
  }

  constexpr const char *UNIQUE_ID_FILE_NAME = "uniqueid.dat";
  constexpr const char *CERTIFICATE_FILE_NAME = "client.pem";
  constexpr const char *PRIVATE_KEY_FILE_NAME = "key.pem";

  struct ReadFileTextResult {
    std::string content;
    int errorCode;
  };

  std::string join_path(const std::string &left, const std::string &right) {
    return platform::join_path(left, right);
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

  bool delete_file_if_present(const std::string &filePath, std::string *errorMessage) {
    if (std::remove(filePath.c_str()) == 0 || errno == ENOENT) {
      return true;
    }

    return append_error(errorMessage, "Failed to delete pairing file '" + filePath + "': " + std::strerror(errno));
  }

}  // namespace

namespace startup {

  std::string default_client_identity_directory() {
    const std::string hostStorageDirectory = platform::parent_directory(default_host_storage_path());
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

  bool delete_client_identity(std::string *errorMessage, const std::string &directoryPath) {
    const std::string uniqueIdPath = join_path(directoryPath, UNIQUE_ID_FILE_NAME);
    const std::string certificatePath = join_path(directoryPath, CERTIFICATE_FILE_NAME);
    const std::string privateKeyPath = join_path(directoryPath, PRIVATE_KEY_FILE_NAME);

    std::string deleteError;
    if (!delete_file_if_present(uniqueIdPath, &deleteError) || !delete_file_if_present(certificatePath, &deleteError) || !delete_file_if_present(privateKeyPath, &deleteError)) {
      return append_error(errorMessage, deleteError);
    }

    return true;
  }

  SaveClientIdentityResult save_client_identity(const network::PairingIdentity &identity, const std::string &directoryPath) {
    std::string errorMessage;
    if (!platform::ensure_directory_exists(directoryPath, &errorMessage)) {
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
