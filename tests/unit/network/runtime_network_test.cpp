#include "src/network/runtime_network.h"

#include <gtest/gtest.h>

namespace {

  TEST(RuntimeNetworkTest, DescribesKnownNxdkInitializationCodes) {
    EXPECT_EQ(network::describe_runtime_network_initialization_code(0), "nxdk networking initialized successfully");
    EXPECT_EQ(network::describe_runtime_network_initialization_code(-1), "nxdk networking could not read or apply the configured network settings");
    EXPECT_EQ(network::describe_runtime_network_initialization_code(-2), "nxdk networking timed out while waiting for DHCP to supply an IPv4 address");
    EXPECT_EQ(network::describe_runtime_network_initialization_code(-99), "nxdk networking failed with an unexpected initialization error");
  }

  TEST(RuntimeNetworkTest, FormatsIpv4DetailsWhenNetworkIsReady) {
    const network::RuntimeNetworkStatus status {
      true,
      true,
      0,
      "nxdk networking initialized successfully",
      "192.168.0.42",
      "255.255.255.0",
      "192.168.0.1",
    };

    const std::vector<std::string> lines = network::format_runtime_network_status_lines(status);

    ASSERT_EQ(lines.size(), 5U);
    EXPECT_EQ(lines[0], "nxdk networking initialized successfully");
    EXPECT_EQ(lines[1], "IPv4 address: 192.168.0.42");
    EXPECT_EQ(lines[2], "Subnet mask: 255.255.255.0");
    EXPECT_EQ(lines[3], "Gateway: 192.168.0.1");
    EXPECT_EQ(lines[4], "Initialization code: 0");
  }

  TEST(RuntimeNetworkTest, FormatsFailureWithoutIpv4Details) {
    const network::RuntimeNetworkStatus status {
      true,
      false,
      -2,
      "nxdk networking timed out while waiting for DHCP to supply an IPv4 address",
      {},
      {},
      {},
    };

    const std::vector<std::string> lines = network::format_runtime_network_status_lines(status);

    ASSERT_EQ(lines.size(), 2U);
    EXPECT_EQ(lines[0], "nxdk networking timed out while waiting for DHCP to supply an IPv4 address");
    EXPECT_EQ(lines[1], "Initialization code: -2");
  }

  TEST(RuntimeNetworkTest, FormatsGatewayWithoutExtraConnectivityHints) {
    const network::RuntimeNetworkStatus status {
      true,
      true,
      0,
      "nxdk networking initialized successfully",
      "10.0.2.15",
      "255.255.255.0",
      "10.0.2.2",
    };

    const std::vector<std::string> lines = network::format_runtime_network_status_lines(status);

    ASSERT_EQ(lines.size(), 5U);
    EXPECT_EQ(lines[3], "Gateway: 10.0.2.2");
    EXPECT_EQ(lines[4], "Initialization code: 0");
  }

}  // namespace
