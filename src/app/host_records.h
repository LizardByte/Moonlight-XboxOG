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
   * @brief Fetch state for the per-host Sunshine app library.
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
    std::string name;
    int id = 0;
    bool hdrSupported = false;
    bool hidden = false;
    bool favorite = false;
    std::string boxArtCacheKey;
    bool boxArtCached = false;
    bool running = false;
  };

  /**
   * @brief Manual host record shown in the shell.
   */
  struct HostRecord {
    std::string displayName;
    std::string address;
    uint16_t port = 0;
    PairingState pairingState = PairingState::not_paired;
    HostReachability reachability = HostReachability::unknown;
    std::string activeAddress;
    std::string uuid;
    std::string localAddress;
    std::string remoteAddress;
    std::string ipv6Address;
    std::string manualAddress;
    std::string macAddress;
    uint16_t httpsPort = 0;
    uint32_t runningGameId = 0;
    std::vector<HostAppRecord> apps;
    HostAppListState appListState = HostAppListState::idle;
    std::string appListStatusMessage;
    uint16_t resolvedHttpPort = 0;
    uint64_t appListContentHash = 0;
    uint32_t lastAppListRefreshTick = 0;
  };

  /**
   * @brief Result of parsing a serialized host record list.
   */
  struct ParseHostRecordsResult {
    std::vector<HostRecord> records;
    std::vector<std::string> errors;
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
   * @param records Host records to serialize.
   * @return Serialized text suitable for a future persistence layer.
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
