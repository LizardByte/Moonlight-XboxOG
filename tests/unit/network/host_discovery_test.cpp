/**
 * @file tests/unit/network/host_discovery_test.cpp
 * @brief Verifies host auto-discovery helpers.
 */
// test header include
#include "src/network/host_discovery.h"

// lib includes
#include <gtest/gtest.h>

namespace {

  class ScopedHostDiscoveryTestHandler {
  public:
    explicit ScopedHostDiscoveryTestHandler(network::testing::HostDiscoveryTestHandler handler) {
      network::testing::set_host_discovery_test_handler(std::move(handler));
    }

    ~ScopedHostDiscoveryTestHandler() {
      network::testing::clear_host_discovery_test_handler();
    }

    ScopedHostDiscoveryTestHandler(const ScopedHostDiscoveryTestHandler &) = delete;
    ScopedHostDiscoveryTestHandler &operator=(const ScopedHostDiscoveryTestHandler &) = delete;
  };

  TEST(HostDiscoveryTest, ReturnsAnEmptySuccessfulResultWithoutATestHandler) {
    const network::DiscoverHostsResult result = network::discover_hosts(250U);

    EXPECT_TRUE(result.hosts.empty());
    EXPECT_TRUE(result.errorMessage.empty());
  }

  TEST(HostDiscoveryTest, UsesTheScriptedTestHandlerWhenInstalled) {
    ScopedHostDiscoveryTestHandler guard([](uint32_t timeoutMilliseconds) {
      EXPECT_EQ(timeoutMilliseconds, 750U);
      return network::DiscoverHostsResult {
        {
          {"Living Room PC", "192.168.1.20", 0},
          {"Office PC", "10.0.0.25", 48000},
        },
        {},
      };
    });

    const network::DiscoverHostsResult result = network::discover_hosts(750U);

    ASSERT_EQ(result.hosts.size(), 2U);
    EXPECT_EQ(result.hosts[0].displayName, "Living Room PC");
    EXPECT_EQ(result.hosts[0].address, "192.168.1.20");
    EXPECT_EQ(result.hosts[1].displayName, "Office PC");
    EXPECT_EQ(result.hosts[1].port, 48000);
    EXPECT_TRUE(result.errorMessage.empty());
  }

}  // namespace
