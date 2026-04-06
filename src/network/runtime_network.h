#pragma once

// standard includes
#include <string>
#include <string_view>
#include <vector>

namespace network {

  struct RuntimeNetworkStatus {
    bool initializationAttempted;
    bool ready;
    int initializationCode;
    std::string summary;
    std::string ipAddress;
    std::string subnetMask;
    std::string gateway;
  };

  RuntimeNetworkStatus initialize_runtime_networking();

  const RuntimeNetworkStatus &runtime_network_status();

  bool runtime_network_ready();

  std::string describe_runtime_network_initialization_code(int initializationCode);

  std::vector<std::string> format_runtime_network_status_lines(const RuntimeNetworkStatus &status);

}  // namespace network
