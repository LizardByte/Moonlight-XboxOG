/**
 * @file src/app/host_records.h
 * @brief Declares host record models and utilities.
 */
#pragma once

// standard includes
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace app {

  inline constexpr uint16_t DEFAULT_HOST_PORT = 47989;

  /**
   * @brief Pairing state tracked for a saved host record.
   */
  enum class PairingState {
    not_paired,
    paired,
  };

  /**
   * @brief Reachability state tracked for a discovered or saved host.
   */
  enum class HostReachability {
    unknown,
    online,
    offline,
  };

  /**
   * @brief Fetch state for the per-host app library.
   */
  enum class HostAppListState {
    idle,
    loading,
    ready,
    failed,
  };

  /**
   * @brief App metadata shown on the per-host apps page.
   */
  struct HostAppRecord {
    std::string name;  ///< Display name reported by the host.
    int id = 0;  ///< Stable host-defined application identifier.
    bool hdrSupported = false;  ///< True when the app advertises HDR streaming support.
    bool hidden = false;  ///< True when the app should be hidden from the default browse view.
    bool favorite = false;  ///< True when the app is pinned as a favorite locally.
    std::string boxArtCacheKey;  ///< Cache key used to load box art from local storage.
    bool boxArtCached = false;  ///< True when the referenced box art is already cached on disk.
    bool running = false;  ///< True when the app is currently running on the host.
  };

  /**
   * @brief Manual host record shown in the shell.
   */
  struct HostRecord {
    std::string displayName;  ///< User-facing host label shown in the shell.
    std::string address;  ///< Stored primary IPv4 address for the host.
    uint16_t port = 0;  ///< Stored HTTP port override where zero means DEFAULT_HOST_PORT.
    PairingState pairingState = PairingState::not_paired;  ///< Current pairing state for this client.
    HostReachability reachability = HostReachability::unknown;  ///< Most recent reachability probe result.
    std::string activeAddress;  ///< Best currently reachable address for live operations.
    std::string uuid;  ///< Host UUID reported by Sunshine or GeForce Experience.
    std::string localAddress;  ///< Reported LAN address from the host status response.
    std::string remoteAddress;  ///< Reported WAN address from the host status response.
    std::string ipv6Address;  ///< Reported IPv6 address from the host status response.
    std::string manualAddress;  ///< Original manually entered address, when different from address.
    std::string macAddress;  ///< Reported MAC address for the host.
    uint16_t httpsPort = 0;  ///< HTTPS port reported by the host for asset requests.
    uint32_t runningGameId = 0;  ///< Currently running app identifier, or zero when idle.
    std::vector<HostAppRecord> apps;  ///< Latest fetched app list for the host.
    HostAppListState appListState = HostAppListState::idle;  ///< Fetch state for the cached app list.
    std::string appListStatusMessage;  ///< User-visible status for the most recent app list operation.
    uint16_t resolvedHttpPort = 0;  ///< Effective HTTP port confirmed by the latest status query.
    uint64_t appListContentHash = 0;  ///< Stable hash of the last fetched app list contents.
    uint32_t lastAppListRefreshTick = 0;  ///< Tick count for the most recent app list refresh.
  };

  /**
   * @brief Result of parsing a serialized host record list.
   */
  struct ParseHostRecordsResult {
    std::vector<HostRecord> records;  ///< Parsed host records accepted from the serialized input.
    std::vector<std::string> errors;  ///< Non-fatal line-level parse or validation errors.
  };

  /**
   * @brief Return a stable lowercase label for a pairing state.
   *
   * @param pairingState Pairing state to stringify.
   * @return Stable lowercase label.
   */
  const char *to_string(PairingState pairingState);

  /**
   * @brief Return a stable lowercase label for a host reachability state.
   *
   * @param reachability Reachability state to stringify.
   * @return Stable lowercase label.
   */
  const char *to_string(HostReachability reachability);

  /**
   * @brief Normalize a user-provided IPv4 address.
   *
   * @param address Candidate IPv4 address.
   * @return Canonical dotted-quad form, or an empty string when invalid.
   */
  std::string normalize_ipv4_address(std::string_view address);

  /**
   * @brief Return whether a string is a valid IPv4 address.
   *
   * @param address Candidate IPv4 address.
   * @return true when the address can be normalized.
   */
  bool is_valid_ipv4_address(std::string_view address);

  /**
   * @brief Build a controller-friendly default display name for a host.
   *
   * @param normalizedAddress Canonical IPv4 address.
   * @return Generated display label.
   */
  std::string build_default_host_display_name(std::string_view normalizedAddress);

  /**
   * @brief Return the effective TCP port for a host record.
   *
   * @param port Stored port override where zero means default.
   * @return Effective host port.
   */
  uint16_t effective_host_port(uint16_t port);

  /**
   * @brief Parse a user-supplied TCP port string.
   *
   * @param portText Text entered by the user.
   * @param parsedPort Output port override where zero means default.
   * @return true when the port string is valid.
   */
  bool try_parse_host_port(std::string_view portText, uint16_t *parsedPort);

  /**
   * @brief Return whether a host list already contains an endpoint.
   *
   * @param records Saved hosts to search.
   * @param normalizedAddress Canonical IPv4 address to match.
   * @param port Stored port override to match.
   * @return true when a saved host uses the same address and effective port.
   */
  bool contains_host_address(const std::vector<HostRecord> &records, std::string_view normalizedAddress, uint16_t port = 0);

  /**
   * @brief Return whether a host record matches a specific endpoint.
   *
   * A host matches when the canonical host address equals @p normalizedAddress and
   * @p port matches any known effective host endpoint (stored HTTP port, resolved
   * HTTP port, or HTTPS port).
   *
   * @param host Host record to test.
   * @param normalizedAddress Canonical IPv4 address to compare.
   * @param port Endpoint port where zero means DEFAULT_HOST_PORT.
   * @return true when the host record can be reached by the given endpoint.
   */
  bool host_matches_endpoint(const HostRecord &host, std::string_view normalizedAddress, uint16_t port);

  /**
   * @brief Validate a host record before saving or serializing it.
   *
   * @param record Host record to validate.
   * @param errorMessage Optional output for a validation error.
   * @return true when the record is valid.
   */
  bool validate_host_record(const HostRecord &record, std::string *errorMessage = nullptr);

  /**
   * @brief Serialize host records into a stable tab-separated text format.
   *
   * The serialized form preserves the saved host identity plus any cached app-list
   * entries and their local visibility or artwork metadata.
   *
   * @param records Host records to serialize.
   * @return Serialized text suitable for disk persistence.
   */
  std::string serialize_host_records(const std::vector<HostRecord> &records);

  /**
   * @brief Parse host records from the stable serialized text format.
   *
   * @param serializedRecords Serialized host record text.
   * @return Parsed records plus any non-fatal line errors.
   */
  ParseHostRecordsResult parse_host_records(std::string_view serializedRecords);

}  // namespace app
