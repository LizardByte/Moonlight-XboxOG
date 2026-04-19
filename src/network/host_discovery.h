/**
 * @file src/network/host_discovery.h
 * @brief Declares host auto-discovery helpers.
 */
#pragma once

// standard includes
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace network {

  /**
   * @brief One Moonlight-compatible host discovered through mDNS/DNS-SD.
   */
  struct DiscoveredHost {
    std::string displayName;  ///< User-facing host name discovered from the service instance.
    std::string address;  ///< Canonical IPv4 address resolved for the host.
    uint16_t port = 0;  ///< Host HTTP port advertised by the discovered service.
  };

  /**
   * @brief Result of one host auto-discovery sweep.
   */
  struct DiscoverHostsResult {
    std::vector<DiscoveredHost> hosts;  ///< Hosts discovered during the sweep.
    std::string errorMessage;  ///< Non-empty when discovery could not run successfully.
  };

  /**
   * @brief Discover Moonlight-compatible hosts on the local network.
   *
   * The Xbox build uses lwIP mDNS/DNS-SD search support to look for compatible
   * GameStream services. Host-native test builds return scripted results when a
   * test handler is installed, or an empty successful result otherwise.
   *
   * @param timeoutMilliseconds Maximum discovery window in milliseconds.
   * @return Discovered hosts plus an optional error message.
   */
  DiscoverHostsResult discover_hosts(uint32_t timeoutMilliseconds);

  namespace testing {

    /**
     * @brief Callback used by unit tests to replace runtime host discovery.
     */
    using HostDiscoveryTestHandler = std::function<DiscoverHostsResult(uint32_t timeoutMilliseconds)>;

    /**
     * @brief Install a scripted host-discovery handler for unit tests.
     *
     * @param handler Callback that should service subsequent discovery requests.
     */
    void set_host_discovery_test_handler(HostDiscoveryTestHandler handler);

    /**
     * @brief Remove any scripted host-discovery handler installed for tests.
     */
    void clear_host_discovery_test_handler();

  }  // namespace testing

}  // namespace network
