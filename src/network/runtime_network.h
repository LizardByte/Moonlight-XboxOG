/**
 * @file src/network/runtime_network.h
 * @brief Declares runtime network status management.
 */
#pragma once

// standard includes
#include <string>
#include <string_view>
#include <vector>

namespace network {

  /**
   * @brief Summary of runtime network initialization and the active IPv4 configuration.
   */
  struct RuntimeNetworkStatus {
    bool initializationAttempted;  ///< True after network initialization has been attempted.
    bool ready;  ///< True when networking is initialized and ready for use.
    int initializationCode;  ///< Platform-specific status code returned by initialization.
    std::string summary;  ///< User-visible summary of the current network state.
    std::string ipAddress;  ///< Active IPv4 address.
    std::string subnetMask;  ///< Active IPv4 subnet mask.
    std::string gateway;  ///< Active IPv4 default gateway.
  };

  /**
   * @brief Initialize runtime networking and cache the resulting status.
   *
   * @return Captured runtime network status after initialization.
   */
  RuntimeNetworkStatus initialize_runtime_networking();

  /**
   * @brief Return the last cached runtime network status.
   *
   * @return Cached runtime network status.
   */
  const RuntimeNetworkStatus &runtime_network_status();

  /**
   * @brief Return whether runtime networking is ready for host communication.
   *
   * @return true when networking is initialized successfully.
   */
  bool runtime_network_ready();

  /**
   * @brief Convert a platform-specific initialization code into readable text.
   *
   * @param initializationCode Platform-specific initialization code.
   * @return Human-readable description of the code.
   */
  std::string describe_runtime_network_initialization_code(int initializationCode);

  /**
   * @brief Format status lines for shell display.
   *
   * @param status Runtime network status to format.
   * @return Display-ready text lines describing the status.
   */
  std::vector<std::string> format_runtime_network_status_lines(const RuntimeNetworkStatus &status);

}  // namespace network
