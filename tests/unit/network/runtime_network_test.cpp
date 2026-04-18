/**
 * @file tests/unit/network/runtime_network_test.cpp
 * @brief Verifies runtime network status management.
 */
// test header include
#include "src/network/runtime_network.h"

// standard includes
#include <string>

// lib includes
#include <gtest/gtest.h>

// test includes
#include "tests/support/network_test_constants.h"

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
      test_support::kTestIpv4Addresses[test_support::kIpRuntimeAddress],
      test_support::kTestIpv4Addresses[test_support::kIpRuntimeSubnetMask],
      test_support::kTestIpv4Addresses[test_support::kIpRuntimeGateway],
    };

    const std::vector<std::string> lines = network::format_runtime_network_status_lines(status);

    ASSERT_EQ(lines.size(), 5U);
    EXPECT_EQ(lines[0], "nxdk networking initialized successfully");
    EXPECT_EQ(lines[1], "IPv4 address: " + std::string(test_support::kTestIpv4Addresses[test_support::kIpRuntimeAddress]));
    EXPECT_EQ(lines[2], "Subnet mask: " + std::string(test_support::kTestIpv4Addresses[test_support::kIpRuntimeSubnetMask]));
    EXPECT_EQ(lines[3], "Gateway: " + std::string(test_support::kTestIpv4Addresses[test_support::kIpRuntimeGateway]));
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
      test_support::kTestIpv4Addresses[test_support::kIpRuntimeDhcpAddress],
      test_support::kTestIpv4Addresses[test_support::kIpRuntimeSubnetMask],
      test_support::kTestIpv4Addresses[test_support::kIpRuntimeDhcpGateway],
    };

    const std::vector<std::string> lines = network::format_runtime_network_status_lines(status);

    ASSERT_EQ(lines.size(), 5U);
    EXPECT_EQ(lines[3], "Gateway: " + std::string(test_support::kTestIpv4Addresses[test_support::kIpRuntimeDhcpGateway]));
    EXPECT_EQ(lines[4], "Initialization code: 0");
  }

  TEST(RuntimeNetworkTest, FormatsOnlyTheStatusFieldsThatArePresent) {
    const network::RuntimeNetworkStatus status {
      false,
      false,
      0,
      {},
      {},
      {},
      test_support::kTestIpv4Addresses[test_support::kIpRuntimeGateway],
    };

    const std::vector<std::string> lines = network::format_runtime_network_status_lines(status);

    ASSERT_EQ(lines.size(), 1U);
    EXPECT_EQ(lines[0], "Gateway: " + std::string(test_support::kTestIpv4Addresses[test_support::kIpRuntimeGateway]));
  }

  TEST(RuntimeNetworkTest, InitializesAndCachesTheHostRuntimeNetworkStatus) {
    const network::RuntimeNetworkStatus initialized = network::initialize_runtime_networking();
    const network::RuntimeNetworkStatus &cached = network::runtime_network_status();

    EXPECT_TRUE(initialized.initializationAttempted);
    EXPECT_TRUE(initialized.ready);
    EXPECT_EQ(initialized.initializationCode, 0);
    EXPECT_EQ(initialized.summary, "Host build networking is provided by the operating system. nxdk network initialization is not required.");
    EXPECT_TRUE(initialized.ipAddress.empty());
    EXPECT_TRUE(initialized.subnetMask.empty());
    EXPECT_TRUE(initialized.gateway.empty());
    EXPECT_TRUE(network::runtime_network_ready());
    EXPECT_EQ(cached.summary, initialized.summary);
    EXPECT_EQ(cached.initializationCode, initialized.initializationCode);
  }

}  // namespace
