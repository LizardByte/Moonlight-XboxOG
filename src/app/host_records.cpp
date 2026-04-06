// class header include
#include "src/app/host_records.h"

// standard includes
#include <utility>
#include <vector>

namespace {

  bool append_error(std::string *errorMessage, std::string message) {
    if (errorMessage != nullptr) {
      *errorMessage = std::move(message);
    }

    return false;
  }

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
    for (std::size_t index = 0; index < segments.size(); ++index) {
      int octetValue = 0;
      if (!parse_ipv4_octet(segments[index], &octetValue)) {
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
    for (const HostRecord &record : records) {
      if (record.address == normalizedAddress && effective_host_port(record.port) == effectivePort) {
        return true;
      }
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
    }
    else if (normalizedAddress != record.address) {
      return append_error(errorMessage, "Host address must already be normalized before saving");
    }

    if (record.port != 0 && effective_host_port(record.port) != record.port) {
      return append_error(errorMessage, "Host port override must be a valid non-zero TCP port");
    }

    return true;
  }

  std::string serialize_host_records(const std::vector<HostRecord> &records) {
    std::string serializedRecords;

    for (const HostRecord &record : records) {
      std::string errorMessage;
      if (!validate_host_record(record, &errorMessage)) {
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
      std::string_view line = lineEnd == std::string_view::npos
        ? serializedRecords.substr(lineStart)
        : serializedRecords.substr(lineStart, lineEnd - lineStart);

      if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
      }

      if (!line.empty()) {
        const std::vector<std::string_view> fields = split_string_view(line, '\t');
        if (fields.size() != 3 && fields.size() != 4) {
          result.errors.push_back("Line " + std::to_string(lineNumber) + " must contain three or four tab-separated fields");
        }
        else {
          uint16_t port = 0;
          PairingState pairingState = PairingState::not_paired;
          const std::string_view pairingField = fields.size() == 4 ? fields[3] : fields[2];
          if (fields.size() == 4 && !try_parse_host_port(fields[2], &port)) {
            result.errors.push_back("Line " + std::to_string(lineNumber) + " uses an invalid TCP port");
            port = 0;
          }

          if (pairingField == "not_paired") {
            pairingState = PairingState::not_paired;
          }
          else if (pairingField == "paired") {
            pairingState = PairingState::paired;
          }
          else {
            result.errors.push_back("Line " + std::to_string(lineNumber) + " uses an unknown pairing state");
            pairingState = PairingState::not_paired;
          }

          HostRecord record {
            std::string(fields[0]),
            std::string(fields[1]),
            port,
            pairingState,
          };

          std::string errorMessage;
          if (validate_host_record(record, &errorMessage)) {
            result.records.push_back(std::move(record));
          }
          else {
            result.errors.push_back("Line " + std::to_string(lineNumber) + ": " + errorMessage);
          }
        }
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
