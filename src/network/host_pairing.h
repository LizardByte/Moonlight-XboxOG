#pragma once

// standard includes
#include <cstdint>
#include <string>
#include <string_view>

namespace network {

  struct PairingIdentity {
    std::string uniqueId;
    std::string certificatePem;
    std::string privateKeyPem;
  };

  struct HostPairingServerInfo {
    int serverMajorVersion;
    uint16_t httpPort;
    uint16_t httpsPort;
    bool paired;
  };

  struct HostPairingRequest {
    std::string address;
    uint16_t httpPort;
    std::string pin;
    std::string deviceName;
    PairingIdentity identity;
  };

  struct HostPairingResult {
    bool success;
    bool alreadyPaired;
    std::string message;
  };

  bool is_valid_pairing_identity(const PairingIdentity &identity);

  PairingIdentity create_pairing_identity(std::string *errorMessage = nullptr);

  bool parse_server_info_response(std::string_view xml, uint16_t fallbackHttpPort, HostPairingServerInfo *serverInfo, std::string *errorMessage = nullptr);

  bool query_server_info(const std::string &address, uint16_t preferredHttpPort, HostPairingServerInfo *serverInfo, std::string *errorMessage = nullptr);

  HostPairingResult pair_host(const HostPairingRequest &request);

}  // namespace network
