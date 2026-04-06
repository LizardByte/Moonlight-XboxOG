#pragma once

// standard includes
#include <string>
#include <vector>

// local includes
#include "src/network/host_pairing.h"

namespace startup {

  struct LoadClientIdentityResult {
    network::PairingIdentity identity;
    std::vector<std::string> warnings;
    bool fileFound;
  };

  struct SaveClientIdentityResult {
    bool success;
    std::string errorMessage;
  };

  std::string default_client_identity_directory();

  LoadClientIdentityResult load_client_identity(const std::string &directoryPath = default_client_identity_directory());

  bool delete_client_identity(
    std::string *errorMessage = nullptr,
    const std::string &directoryPath = default_client_identity_directory()
  );

  SaveClientIdentityResult save_client_identity(
    const network::PairingIdentity &identity,
    const std::string &directoryPath = default_client_identity_directory()
  );

}  // namespace startup
