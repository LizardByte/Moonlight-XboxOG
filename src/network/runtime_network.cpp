// class header include
#include "src/network/runtime_network.h"


// nxdk includes
#ifdef NXDK
#include <lwip/ip4_addr.h>
#include <lwip/netif.h>
#include <nxdk/net.h>

extern "C" struct netif *g_pnetif;
#endif

namespace {

  network::RuntimeNetworkStatus g_runtimeNetworkStatus {};

#ifdef NXDK
  std::string copy_ipv4_string(const ip4_addr_t *address) {
    if (address == nullptr) {
      return {};
    }

    const char *text = ip4addr_ntoa(address);
    return text == nullptr ? std::string {} : std::string(text);
  }
#endif

  network::RuntimeNetworkStatus make_host_network_status() {
    return {
      true,
      true,
      0,
      "Host build networking is provided by the operating system. nxdk network initialization is not required.",
      {},
      {},
      {},
    };
  }

}  // namespace

namespace network {

  std::string describe_runtime_network_initialization_code(int initializationCode) {
    switch (initializationCode) {
      case 0:
        return "nxdk networking initialized successfully";
      case -1:
        return "nxdk networking could not read or apply the configured network settings";
      case -2:
        return "nxdk networking timed out while waiting for DHCP to supply an IPv4 address";
      default:
        return "nxdk networking failed with an unexpected initialization error";
    }
  }

  std::vector<std::string> format_runtime_network_status_lines(const RuntimeNetworkStatus &status) {
    std::vector<std::string> lines;
    if (!status.summary.empty()) {
      lines.push_back(status.summary);
    }

    if (!status.ipAddress.empty()) {
      lines.push_back("IPv4 address: " + status.ipAddress);
    }
    if (!status.subnetMask.empty()) {
      lines.push_back("Subnet mask: " + status.subnetMask);
    }
    if (!status.gateway.empty()) {
      lines.push_back("Gateway: " + status.gateway);
    }

    if (status.initializationAttempted) {
      lines.push_back("Initialization code: " + std::to_string(status.initializationCode));
    }

    return lines;
  }

  RuntimeNetworkStatus initialize_runtime_networking() {
    if (g_runtimeNetworkStatus.initializationAttempted) {
      return g_runtimeNetworkStatus;
    }

#ifdef NXDK
    g_runtimeNetworkStatus.initializationAttempted = true;
    g_runtimeNetworkStatus.initializationCode = nxNetInit(nullptr);
    g_runtimeNetworkStatus.ready = g_runtimeNetworkStatus.initializationCode == 0;
    g_runtimeNetworkStatus.summary = describe_runtime_network_initialization_code(g_runtimeNetworkStatus.initializationCode);

    if (g_runtimeNetworkStatus.ready && g_pnetif != nullptr) {
      g_runtimeNetworkStatus.ipAddress = copy_ipv4_string(netif_ip4_addr(g_pnetif));
      g_runtimeNetworkStatus.subnetMask = copy_ipv4_string(netif_ip4_netmask(g_pnetif));
      g_runtimeNetworkStatus.gateway = copy_ipv4_string(netif_ip4_gw(g_pnetif));
    }
    else if (g_runtimeNetworkStatus.ready) {
      g_runtimeNetworkStatus.ready = false;
      g_runtimeNetworkStatus.initializationCode = -3;
      g_runtimeNetworkStatus.summary = "nxdk networking initialized without publishing an lwIP network interface";
    }

#else
    g_runtimeNetworkStatus = make_host_network_status();
#endif

    return g_runtimeNetworkStatus;
  }

  const RuntimeNetworkStatus &runtime_network_status() {
    initialize_runtime_networking();
    return g_runtimeNetworkStatus;
  }

  bool runtime_network_ready() {
    return runtime_network_status().ready;
  }

}  // namespace network
