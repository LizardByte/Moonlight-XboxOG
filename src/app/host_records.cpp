/**
 * @file src/app/host_records.cpp
 * @brief Implements host record models and utilities.
 */
// class header include
#include "src/app/host_records.h"

// standard includes
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

// local includes
#include "src/platform/error_utils.h"

namespace {

  using platform::append_error;

  bool parse_ipv4_octet(std::string_view segment, int *value) {
    if (segment.empty()) {
      return false;
    }

    int parsedValue = 0;
    for (char character : segment) {
      if (character < '0' || character > '9') {
        return false;
      }

      parsedValue = (parsedValue * 10) + (character - '0');
      if (parsedValue > 255) {
        return false;
      }
    }

    if (value != nullptr) {
      *value = parsedValue;
    }

    return true;
  }

  std::vector<std::string_view> split_string_view(std::string_view text, char delimiter) {
    std::vector<std::string_view> segments;
    std::size_t startIndex = 0;

    while (startIndex <= text.size()) {
      const std::size_t delimiterIndex = text.find(delimiter, startIndex);
      if (delimiterIndex == std::string_view::npos) {
        segments.push_back(text.substr(startIndex));
        break;
      }

      segments.push_back(text.substr(startIndex, delimiterIndex - startIndex));
      startIndex = delimiterIndex + 1;
    }

    return segments;
  }

  char hex_digit(std::byte value) {
    const unsigned int digit = std::to_integer<unsigned int>(value);
    return static_cast<char>(digit < 10U ? ('0' + digit) : ('A' + (digit - 10U)));
  }

  void append_percent_encoded_byte(std::string *encoded, unsigned char character) {
    if (encoded == nullptr) {
      return;
    }

    const auto byteValue = static_cast<std::byte>(character);
    encoded->push_back('%');
    encoded->push_back(hex_digit((byteValue >> 4U) & std::byte {0x0F}));
    encoded->push_back(hex_digit(byteValue & std::byte {0x0F}));
  }

  bool is_unreserved_serialized_character(unsigned char character) {
    return std::isalnum(character) != 0 || character == '-' || character == '_' || character == '.';
  }

  std::string percent_encode(std::string_view text) {
    std::string encoded;
    encoded.reserve(text.size());

    for (const unsigned char character : text) {
      if (is_unreserved_serialized_character(character)) {
        encoded.push_back(static_cast<char>(character));
        continue;
      }

      append_percent_encoded_byte(&encoded, character);
    }

    return encoded;
  }

  int hex_value(char character) {
    if (character >= '0' && character <= '9') {
      return character - '0';
    }
    if (character >= 'A' && character <= 'F') {
      return 10 + (character - 'A');
    }
    if (character >= 'a' && character <= 'f') {
      return 10 + (character - 'a');
    }
    return -1;
  }

  bool percent_decode(std::string_view text, std::string *decoded) {
    if (decoded == nullptr) {
      return false;
    }

    std::string result;
    result.reserve(text.size());
    std::size_t index = 0;
    while (index < text.size()) {
      if (text[index] != '%') {
        result.push_back(text[index]);
        ++index;
        continue;
      }

      if (index + 2U >= text.size()) {
        return false;
      }

      const int highNibble = hex_value(text[index + 1U]);
      const int lowNibble = hex_value(text[index + 2U]);
      if (highNibble < 0 || lowNibble < 0) {
        return false;
      }

      result.push_back(static_cast<char>((highNibble << 4U) | lowNibble));
      index += 3U;
    }

    *decoded = std::move(result);
    return true;
  }

  bool try_parse_unsigned_integer(std::string_view text, uint64_t maxValue, uint64_t *value) {
    if (text.empty()) {
      return false;
    }

    uint64_t parsedValue = 0;
    for (char character : text) {
      if (character < '0' || character > '9') {
        return false;
      }

      const auto digit = static_cast<uint64_t>(character - '0');
      if (parsedValue > ((maxValue - digit) / 10U)) {
        return false;
      }
      parsedValue = (parsedValue * 10U) + digit;
    }

    if (value != nullptr) {
      *value = parsedValue;
    }
    return true;
  }

  bool try_parse_serialized_boolean(std::string_view text, bool *value) {
    if (text == "0") {
      if (value != nullptr) {
        *value = false;
      }
      return true;
    }
    if (text == "1") {
      if (value != nullptr) {
        *value = true;
      }
      return true;
    }
    return false;
  }

  std::string serialize_cached_host_metadata(const app::HostRecord &record) {
    return std::to_string(record.runningGameId) + ',' + std::to_string(record.resolvedHttpPort) + ',' + std::to_string(record.httpsPort) + ',' + std::to_string(record.appListContentHash);
  }

  bool parse_cached_host_metadata(std::string_view serializedMetadata, app::HostRecord *record) {
    if (record == nullptr || serializedMetadata.empty()) {
      return record != nullptr;
    }

    const std::vector<std::string_view> fields = split_string_view(serializedMetadata, ',');
    if (fields.size() != 4U) {
      return false;
    }

    uint64_t runningGameId = 0;
    uint64_t resolvedHttpPort = 0;
    uint64_t httpsPort = 0;
    uint64_t appListContentHash = 0;
    if (!try_parse_unsigned_integer(fields[0], std::numeric_limits<uint32_t>::max(), &runningGameId) ||
        !try_parse_unsigned_integer(fields[1], std::numeric_limits<uint16_t>::max(), &resolvedHttpPort) ||
        !try_parse_unsigned_integer(fields[2], std::numeric_limits<uint16_t>::max(), &httpsPort) ||
        !try_parse_unsigned_integer(fields[3], std::numeric_limits<uint64_t>::max(), &appListContentHash)) {
      return false;
    }

    record->runningGameId = static_cast<uint32_t>(runningGameId);
    record->resolvedHttpPort = static_cast<uint16_t>(resolvedHttpPort);
    record->httpsPort = static_cast<uint16_t>(httpsPort);
    record->appListContentHash = appListContentHash;
    return true;
  }

  std::string serialize_cached_app_record(const app::HostAppRecord &record) {
    return percent_encode(record.name) + ',' + std::to_string(record.id) + ',' + (record.hdrSupported ? "1" : "0") + ',' + (record.hidden ? "1" : "0") + ',' +
           (record.favorite ? "1" : "0") + ',' + percent_encode(record.boxArtCacheKey) + ',' + (record.boxArtCached ? "1" : "0");
  }

  bool parse_cached_app_record(std::string_view serializedApp, app::HostAppRecord *record) {
    if (record == nullptr || serializedApp.empty()) {
      return false;
    }

    const std::vector<std::string_view> fields = split_string_view(serializedApp, ',');
    if (fields.size() != 7U) {
      return false;
    }

    std::string name;
    std::string boxArtCacheKey;
    uint64_t id = 0;
    bool hdrSupported = false;
    bool hidden = false;
    bool favorite = false;
    bool boxArtCached = false;
    if (!percent_decode(fields[0], &name) || !try_parse_unsigned_integer(fields[1], static_cast<uint64_t>(std::numeric_limits<int>::max()), &id) || !try_parse_serialized_boolean(fields[2], &hdrSupported) ||
        !try_parse_serialized_boolean(fields[3], &hidden) || !try_parse_serialized_boolean(fields[4], &favorite) || !percent_decode(fields[5], &boxArtCacheKey) || !try_parse_serialized_boolean(fields[6], &boxArtCached)) {
      return false;
    }

    *record = {
      std::move(name),
      static_cast<int>(id),
      hdrSupported,
      hidden,
      favorite,
      std::move(boxArtCacheKey),
      boxArtCached,
      false,
    };
    return true;
  }

  std::string serialize_cached_app_list(const std::vector<app::HostAppRecord> &apps) {
    std::string serializedApps;
    for (std::size_t index = 0; index < apps.size(); ++index) {
      if (index > 0U) {
        serializedApps += '|';
      }
      serializedApps += serialize_cached_app_record(apps[index]);
    }
    return serializedApps;
  }

  bool parse_cached_app_list(std::string_view serializedApps, std::vector<app::HostAppRecord> *apps) {
    if (apps == nullptr) {
      return false;
    }
    if (serializedApps.empty()) {
      apps->clear();
      return true;
    }

    std::vector<app::HostAppRecord> parsedApps;
    for (const std::string_view serializedApp : split_string_view(serializedApps, '|')) {
      app::HostAppRecord parsedApp {};
      if (!parse_cached_app_record(serializedApp, &parsedApp)) {
        return false;
      }
      parsedApps.push_back(std::move(parsedApp));
    }

    *apps = std::move(parsedApps);
    return true;
  }

}  // namespace

namespace app {

  const char *to_string(PairingState pairingState) {
    switch (pairingState) {
      case PairingState::not_paired:
        return "not_paired";
      case PairingState::paired:
        return "paired";
    }

    return "unknown";
  }

  const char *to_string(HostReachability reachability) {
    switch (reachability) {
      case HostReachability::unknown:
        return "unknown";
      case HostReachability::online:
        return "online";
      case HostReachability::offline:
        return "offline";
    }

    return "unknown";
  }

  std::string normalize_ipv4_address(std::string_view address) {
    const std::vector<std::string_view> segments = split_string_view(address, '.');
    if (segments.size() != 4) {
      return {};
    }

    std::string normalizedAddress;
    for (std::string_view segment : segments) {
      int octetValue = 0;
      if (!parse_ipv4_octet(segment, &octetValue)) {
        return {};
      }

      if (!normalizedAddress.empty()) {
        normalizedAddress += '.';
      }
      normalizedAddress += std::to_string(octetValue);
    }

    return normalizedAddress;
  }

  bool is_valid_ipv4_address(std::string_view address) {
    return !normalize_ipv4_address(address).empty();
  }

  std::string build_default_host_display_name(std::string_view normalizedAddress) {
    return std::string("Host ") + std::string(normalizedAddress);
  }

  uint16_t effective_host_port(uint16_t port) {
    return port == 0 ? DEFAULT_HOST_PORT : port;
  }

  bool try_parse_host_port(std::string_view portText, uint16_t *parsedPort) {
    if (portText.empty()) {
      if (parsedPort != nullptr) {
        *parsedPort = 0;
      }
      return true;
    }

    unsigned long parsedValue = 0;
    for (char character : portText) {
      if (character < '0' || character > '9') {
        return false;
      }

      parsedValue = (parsedValue * 10) + static_cast<unsigned long>(character - '0');
      if (parsedValue > 65535UL) {
        return false;
      }
    }

    if (parsedValue == 0) {
      return false;
    }

    if (parsedPort != nullptr) {
      *parsedPort = static_cast<uint16_t>(parsedValue);
    }

    return true;
  }

  bool contains_host_address(const std::vector<HostRecord> &records, std::string_view normalizedAddress, uint16_t port) {
    const uint16_t effectivePort = effective_host_port(port);
    return std::any_of(records.begin(), records.end(), [normalizedAddress, effectivePort](const HostRecord &record) {
      return record.address == normalizedAddress && effective_host_port(record.port) == effectivePort;
    });
  }

  bool host_matches_endpoint(const HostRecord &host, std::string_view normalizedAddress, uint16_t port) {
    if (host.address != normalizedAddress) {
      return false;
    }

    const uint16_t effectivePort = effective_host_port(port);
    if (effective_host_port(host.port) == effectivePort) {
      return true;
    }
    if (host.resolvedHttpPort != 0 && host.resolvedHttpPort == effectivePort) {
      return true;
    }
    if (host.httpsPort != 0 && host.httpsPort == effectivePort) {
      return true;
    }
    return false;
  }

  bool validate_host_record(const HostRecord &record, std::string *errorMessage) {
    if (record.displayName.empty()) {
      return append_error(errorMessage, "Host display name cannot be empty");
    }

    if (record.displayName.find('\t') != std::string::npos || record.displayName.find('\n') != std::string::npos || record.displayName.find('\r') != std::string::npos) {
      return append_error(errorMessage, "Host display name cannot contain tabs or new lines");
    }

    if (const std::string normalizedAddress = normalize_ipv4_address(record.address); normalizedAddress.empty()) {
      return append_error(errorMessage, "Host address must be a valid IPv4 address");
    } else if (normalizedAddress != record.address) {
      return append_error(errorMessage, "Host address must already be normalized before saving");
    }

    if (record.port != 0 && effective_host_port(record.port) != record.port) {
      return append_error(errorMessage, "Host port override must be a valid non-zero TCP port");
    }

    return true;
  }

  void append_parsed_host_record(std::string_view line, std::size_t lineNumber, ParseHostRecordsResult *result) {
    if (result == nullptr) {
      return;
    }

    const std::vector<std::string_view> fields = split_string_view(line, '\t');
    if (fields.size() != 6U) {
      result->errors.push_back("Line " + std::to_string(lineNumber) + " must contain six tab-separated fields");
      return;
    }

    uint16_t port = 0;
    const std::string_view pairingField = fields[3];
    if (!try_parse_host_port(fields[2], &port)) {
      result->errors.push_back("Line " + std::to_string(lineNumber) + " uses an invalid TCP port");
      return;
    }

    PairingState pairingState = PairingState::not_paired;
    if (pairingField == "paired") {
      pairingState = PairingState::paired;
    } else if (pairingField != "not_paired") {
      result->errors.push_back("Line " + std::to_string(lineNumber) + " uses an unknown pairing state");
      return;
    }

    HostRecord record {
      std::string(fields[0]),
      std::string(fields[1]),
      port,
      pairingState,
    };

    if (std::string errorMessage; !validate_host_record(record, &errorMessage)) {
      result->errors.push_back("Line " + std::to_string(lineNumber) + ": " + errorMessage);
      return;
    }

    HostRecord cachedRecord = record;
    if (!parse_cached_host_metadata(fields[4], &cachedRecord) || !parse_cached_app_list(fields[5], &cachedRecord.apps)) {
      result->errors.push_back("Line " + std::to_string(lineNumber) + " contains malformed cached app data");
      return;
    }

    cachedRecord.appListState = cachedRecord.apps.empty() ? HostAppListState::idle : HostAppListState::ready;
    cachedRecord.appListStatusMessage.clear();
    cachedRecord.lastAppListRefreshTick = 0U;
    for (HostAppRecord &appRecord : cachedRecord.apps) {
      appRecord.running = static_cast<uint32_t>(appRecord.id) == cachedRecord.runningGameId;
    }
    record = std::move(cachedRecord);

    result->records.push_back(std::move(record));
  }

  std::string serialize_host_records(const std::vector<HostRecord> &records) {
    std::string serializedRecords;

    for (const HostRecord &record : records) {
      if (std::string errorMessage; !validate_host_record(record, &errorMessage)) {
        continue;
      }

      serializedRecords += record.displayName;
      serializedRecords += '\t';
      serializedRecords += record.address;
      serializedRecords += '\t';
      if (record.port != 0) {
        serializedRecords += std::to_string(record.port);
      }
      serializedRecords += '\t';
      serializedRecords += to_string(record.pairingState);
      serializedRecords += '\t';
      serializedRecords += serialize_cached_host_metadata(record);
      serializedRecords += '\t';
      serializedRecords += serialize_cached_app_list(record.apps);
      serializedRecords += '\n';
    }

    return serializedRecords;
  }

  ParseHostRecordsResult parse_host_records(std::string_view serializedRecords) {
    ParseHostRecordsResult result {};

    std::size_t lineStart = 0;
    std::size_t lineNumber = 1;
    while (lineStart <= serializedRecords.size()) {
      const std::size_t lineEnd = serializedRecords.find('\n', lineStart);
      std::string_view line = lineEnd == std::string_view::npos ? serializedRecords.substr(lineStart) : serializedRecords.substr(lineStart, lineEnd - lineStart);

      if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
      }

      if (!line.empty()) {
        append_parsed_host_record(line, lineNumber, &result);
      }

      if (lineEnd == std::string_view::npos) {
        break;
      }

      lineStart = lineEnd + 1;
      ++lineNumber;
    }

    return result;
  }

}  // namespace app
