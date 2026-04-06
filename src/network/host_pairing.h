#pragma once

// standard includes
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace network {

  struct PairingIdentity {
    std::string uniqueId;
    std::string certificatePem;
    std::string privateKeyPem;
  };

  struct HostPairingServerInfo {
    int serverMajorVersion = 0;
    uint16_t httpPort = 0;
    uint16_t httpsPort = 0;
    bool paired = false;
    bool pairingStatusCurrentClientKnown = false;
    bool pairingStatusCurrentClient = false;
    std::string hostName;
    std::string uuid;
    std::string activeAddress;
    std::string localAddress;
    std::string remoteAddress;
    std::string ipv6Address;
    std::string macAddress;
    uint32_t runningGameId = 0;
  };

  struct HostAppEntry {
    std::string name;
    int id = 0;
    bool hdrSupported = false;
    bool hidden = false;
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

  bool parse_app_list_response(std::string_view xml, std::vector<HostAppEntry> *apps, std::string *errorMessage = nullptr);

  bool error_indicates_unpaired_client(std::string_view errorMessage);

  uint64_t hash_app_list_entries(const std::vector<HostAppEntry> &apps);

  std::string resolve_reachable_address(const std::string &requestedAddress, const HostPairingServerInfo &serverInfo);

  bool query_server_info(
    const std::string &address,
    uint16_t preferredHttpPort,
    const PairingIdentity *clientIdentity,
    HostPairingServerInfo *serverInfo,
    std::string *errorMessage = nullptr
  );

  bool query_server_info(const std::string &address, uint16_t preferredHttpPort, HostPairingServerInfo *serverInfo, std::string *errorMessage = nullptr);

  bool query_app_list(
    const std::string &address,
    uint16_t preferredHttpPort,
    const PairingIdentity *clientIdentity,
    std::vector<HostAppEntry> *apps,
    HostPairingServerInfo *serverInfo = nullptr,
    std::string *errorMessage = nullptr
  );

  bool query_app_asset(
    const std::string &address,
    uint16_t httpsPort,
    const PairingIdentity *clientIdentity,
    int appId,
    std::vector<unsigned char> *assetBytes,
    std::string *errorMessage = nullptr
  );

  HostPairingResult pair_host(const HostPairingRequest &request, const std::atomic<bool> *cancelRequested = nullptr);

}  // namespace network
