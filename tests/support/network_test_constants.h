#pragma once

// standard includes
#include <array>
#include <cstddef>
#include <cstdint>

namespace test_support {

  /// Shared IPv4 fixtures used by unit tests.
  inline constexpr std::array<const char *, 21> kTestIpv4Addresses = {
    "192.168.1.20",  // NOSONAR(cpp:S1313) test fixtures intentionally use concrete IPv4 values.
    "10.0.0.25",  // NOSONAR(cpp:S1313) test fixtures intentionally use concrete IPv4 values.
    "193.168.1.10",  // NOSONAR(cpp:S1313) test fixtures intentionally use concrete IPv4 values.
    "192.168.0.10",  // NOSONAR(cpp:S1313) test fixtures intentionally use concrete IPv4 values.
    "192.168.0.11",  // NOSONAR(cpp:S1313) test fixtures intentionally use concrete IPv4 values.
    "192.168.0.12",  // NOSONAR(cpp:S1313) test fixtures intentionally use concrete IPv4 values.
    "192.168.0.13",  // NOSONAR(cpp:S1313) test fixtures intentionally use concrete IPv4 values.
    "192.168.0.14",  // NOSONAR(cpp:S1313) test fixtures intentionally use concrete IPv4 values.
    "10.0.0.15",  // NOSONAR(cpp:S1313) test fixtures intentionally use concrete IPv4 values.
    "192.168.1.21",  // NOSONAR(cpp:S1313) test fixtures intentionally use concrete IPv4 values.
    "192.168.1.25",  // NOSONAR(cpp:S1313) test fixtures intentionally use concrete IPv4 values.
    "203.0.113.7",  // NOSONAR(cpp:S1313) test fixtures intentionally use concrete IPv4 values.
    "203.0.113.9",  // NOSONAR(cpp:S1313) test fixtures intentionally use concrete IPv4 values.
    "127.0.0.1",  // NOSONAR(cpp:S1313) test fixtures intentionally use concrete IPv4 values.
    "192.168.0.50",  // NOSONAR(cpp:S1313) test fixtures intentionally use concrete IPv4 values.
    "192.168.0.51",  // NOSONAR(cpp:S1313) test fixtures intentionally use concrete IPv4 values.
    "192.168.0.42",  // NOSONAR(cpp:S1313) test fixtures intentionally use concrete IPv4 values.
    "255.255.255.0",  // NOSONAR(cpp:S1313) test fixtures intentionally use concrete IPv4 values.
    "192.168.0.1",  // NOSONAR(cpp:S1313) test fixtures intentionally use concrete IPv4 values.
    "10.0.2.15",  // NOSONAR(cpp:S1313) test fixtures intentionally use concrete IPv4 values.
    "10.0.2.2",  // NOSONAR(cpp:S1313) test fixtures intentionally use concrete IPv4 values.
  };

  /// Index for `kTestIpv4Addresses` entries used by tests.
  inline constexpr std::size_t kIpLivingRoom = 0U;
  inline constexpr std::size_t kIpOffice = 1U;
  inline constexpr std::size_t kIpManualCustomPort = 2U;
  inline constexpr std::size_t kIpHostGridA = 3U;
  inline constexpr std::size_t kIpHostGridB = 4U;
  inline constexpr std::size_t kIpHostGridC = 5U;
  inline constexpr std::size_t kIpHostGridD = 6U;
  inline constexpr std::size_t kIpHostGridE = 7U;
  inline constexpr std::size_t kIpSteamDeckDock = 8U;
  inline constexpr std::size_t kIpLivingRoomNeighbor = 9U;
  inline constexpr std::size_t kIpServerLocal = 10U;
  inline constexpr std::size_t kIpServerExternal = 11U;
  inline constexpr std::size_t kIpServerExternalAlt = 12U;
  inline constexpr std::size_t kIpLoopback = 13U;
  inline constexpr std::size_t kIpExternalFallback = 14U;
  inline constexpr std::size_t kIpLocalFallback = 15U;
  inline constexpr std::size_t kIpRuntimeAddress = 16U;
  inline constexpr std::size_t kIpRuntimeSubnetMask = 17U;
  inline constexpr std::size_t kIpRuntimeGateway = 18U;
  inline constexpr std::size_t kIpRuntimeDhcpAddress = 19U;
  inline constexpr std::size_t kIpRuntimeDhcpGateway = 20U;

  /// Shared host port fixtures used by unit tests.
  inline constexpr std::array<uint16_t, 4> kTestPorts = {
    48000,
    47984,
    47989,
    47990,
  };

  /// Index for `kTestPorts` entries used by tests.
  inline constexpr std::size_t kPortDefaultHost = 0U;
  inline constexpr std::size_t kPortPairing = 1U;
  inline constexpr std::size_t kPortResolvedHttp = 2U;
  inline constexpr std::size_t kPortResolvedHttps = 3U;

}  // namespace test_support
