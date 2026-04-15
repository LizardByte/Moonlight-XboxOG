/**
 * @file src/network/host_pairing.h
 * @brief Declares host pairing helpers.
 */
#pragma once

// standard includes
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace network {

  /**
   * @brief Client identity material used for Moonlight host pairing.
   */
  struct PairingIdentity {
    std::string uniqueId;  ///< Stable unique identifier presented to the host.
    std::string certificatePem;  ///< Client certificate in PEM format.
    std::string privateKeyPem;  ///< Private key matching the client certificate in PEM format.
  };

  /**
   * @brief Parsed host status fields used by browsing and pairing flows.
   */
  struct HostPairingServerInfo {
    int serverMajorVersion = 0;  ///< Major version reported by the host server.
    uint16_t httpPort = 0;  ///< HTTP port reported or inferred for plaintext requests.
    uint16_t httpsPort = 0;  ///< HTTPS port reported for encrypted requests and assets.
    bool paired = false;  ///< True when the host reports that this client is paired.
    bool pairingStatusCurrentClientKnown = false;  ///< True when the host explicitly reported pairing status for this client.
    bool pairingStatusCurrentClient = false;  ///< Pairing status reported for this specific client identity.
    std::string hostName;  ///< User-facing host name reported by the server.
    std::string uuid;  ///< Stable host UUID.
    std::string activeAddress;  ///< Best currently reachable address chosen for live requests.
    std::string localAddress;  ///< Host-reported LAN address.
    std::string remoteAddress;  ///< Host-reported WAN address.
    std::string ipv6Address;  ///< Host-reported IPv6 address.
    std::string macAddress;  ///< Host-reported MAC address.
    uint32_t runningGameId = 0;  ///< Running game identifier, or zero when idle.
  };

  /**
   * @brief One application entry returned by the host app list API.
   */
  struct HostAppEntry {
    std::string name;  ///< Display name reported by the host.
    int id = 0;  ///< Stable host-defined application identifier.
    bool hdrSupported = false;  ///< True when the app advertises HDR support.
    bool hidden = false;  ///< True when the app is hidden by default on the host.
  };

  /**
   * @brief Parameters required to perform a host pairing request.
   */
  struct HostPairingRequest {
    std::string address;  ///< Host address used for the pairing session.
    uint16_t httpPort;  ///< Effective host HTTP port used for pairing.
    std::string pin;  ///< User-entered or generated pairing PIN.
    std::string deviceName;  ///< Friendly client name presented to the host.
    PairingIdentity identity;  ///< Client identity material presented during pairing.
  };

  /**
   * @brief Outcome of attempting to pair with a host.
   */
  struct HostPairingResult {
    bool success;  ///< True when pairing succeeded.
    bool alreadyPaired;  ///< True when the host was already paired before the request.
    std::string message;  ///< User-visible success or failure detail.
  };

  /**
   * @brief Return whether a pairing identity contains the required PEM materials.
   *
   * @param identity Candidate pairing identity.
   * @return true when the identity is usable for authenticated requests.
   */
  bool is_valid_pairing_identity(const PairingIdentity &identity);

  /**
   * @brief Create a fresh client identity for Moonlight pairing.
   *
   * @param errorMessage Optional output for key or certificate generation failures.
   * @return Generated pairing identity, or an empty identity on failure.
   */
  PairingIdentity create_pairing_identity(std::string *errorMessage = nullptr);

  /**
   * @brief Generate a secure four-digit PIN for host pairing.
   *
   * @param pin Output buffer populated with exactly four decimal digits.
   * @param errorMessage Optional output for entropy or random-byte failures.
   * @return true when a secure PIN was generated successfully.
   */
  bool generate_pairing_pin(std::string *pin, std::string *errorMessage = nullptr);

  /**
   * @brief Parse the XML response returned by the host server-info endpoint.
   *
   * @param xml Raw XML response body.
   * @param fallbackHttpPort HTTP port to use when the response omits it.
   * @param serverInfo Output structure populated from the response.
   * @param errorMessage Optional output for parse or validation failures.
   * @return true when the response was parsed successfully.
   */
  bool parse_server_info_response(std::string_view xml, uint16_t fallbackHttpPort, HostPairingServerInfo *serverInfo, std::string *errorMessage = nullptr);

  /**
   * @brief Parse the XML response returned by the host app-list endpoint.
   *
   * @param xml Raw XML response body.
   * @param apps Output vector populated with parsed app entries.
   * @param errorMessage Optional output for parse or validation failures.
   * @return true when the response was parsed successfully.
   */
  bool parse_app_list_response(std::string_view xml, std::vector<HostAppEntry> *apps, std::string *errorMessage = nullptr);

  /**
   * @brief Return whether an error message indicates the client is not paired.
   *
   * @param errorMessage Candidate error text.
   * @return true when the text maps to an unpaired-client condition.
   */
  bool error_indicates_unpaired_client(std::string_view errorMessage);

  /**
   * @brief Compute a stable hash for a fetched app list.
   *
   * @param apps App entries to hash.
   * @return Stable hash value for content-change detection.
   */
  uint64_t hash_app_list_entries(const std::vector<HostAppEntry> &apps);

  /**
   * @brief Choose the best reachable address for a host.
   *
   * @param requestedAddress Address originally requested by the user.
   * @param serverInfo Parsed host status information.
   * @return Reachable address to use for subsequent requests.
   */
  std::string resolve_reachable_address(const std::string &requestedAddress, const HostPairingServerInfo &serverInfo);

  /**
   * @brief Query live host status using an optional client identity.
   *
   * @param address Host address to query.
   * @param preferredHttpPort Preferred HTTP port override.
   * @param clientIdentity Optional client identity for authenticated requests.
   * @param serverInfo Output structure populated with parsed status data.
   * @param errorMessage Optional output for request or parse failures.
   * @return true when host status was retrieved successfully.
   */
  bool query_server_info(
    const std::string &address,
    uint16_t preferredHttpPort,
    const PairingIdentity *clientIdentity,
    HostPairingServerInfo *serverInfo,
    std::string *errorMessage = nullptr
  );

  /**
   * @brief Query the app list exported by a host.
   *
   * @param address Host address to query.
   * @param preferredHttpPort Preferred HTTP port override.
   * @param clientIdentity Optional client identity for authenticated requests.
   * @param apps Output vector populated with parsed app entries.
   * @param serverInfo Optional output populated with the latest host status data.
   * @param errorMessage Optional output for request or parse failures.
   * @return true when the app list was retrieved successfully.
   */
  bool query_app_list(
    const std::string &address,
    uint16_t preferredHttpPort,
    const PairingIdentity *clientIdentity,
    std::vector<HostAppEntry> *apps,
    HostPairingServerInfo *serverInfo = nullptr,
    std::string *errorMessage = nullptr
  );

  /**
   * @brief Query one app asset such as box art over HTTPS.
   *
   * @param address Host address to query.
   * @param httpsPort Host HTTPS port.
   * @param clientIdentity Optional client identity for authenticated requests.
   * @param appId Host application identifier.
   * @param assetBytes Output vector populated with downloaded asset bytes.
   * @param errorMessage Optional output for request failures.
   * @return true when the asset was downloaded successfully.
   */
  bool query_app_asset(
    const std::string &address,
    uint16_t httpsPort,
    const PairingIdentity *clientIdentity,
    int appId,
    std::vector<unsigned char> *assetBytes,
    std::string *errorMessage = nullptr
  );

  /**
   * @brief Pair the client with a host using the provided request parameters.
   *
   * @param request Pairing parameters and client identity.
   * @param cancelRequested Optional cancellation flag checked during the request.
   * @return Pairing outcome including success state and user-visible detail.
   */
  HostPairingResult pair_host(const HostPairingRequest &request, const std::atomic<bool> *cancelRequested = nullptr);

}  // namespace network
