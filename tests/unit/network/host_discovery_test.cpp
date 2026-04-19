/**
 * @file tests/unit/network/host_discovery_test.cpp
 * @brief Verifies host auto-discovery helpers.
 */
// test header include
#include "src/network/host_discovery.h"

// standard includes
#include <string_view>

// lib includes
#include <gtest/gtest.h>

// test includes
#include "tests/support/network_test_constants.h"

namespace network::testing {

  std::string trim_host_discovery_text(std::string_view text);
  std::string first_host_discovery_label(std::string_view domainText);
  bool append_mdns_discovered_host(::network::DiscoverHostsResult *result, std::string displayName, const std::string &address, uint16_t port);
  void sort_discovered_hosts(std::vector<::network::DiscoveredHost> *hosts);

}  // namespace network::testing

namespace {

  namespace host_discovery_testing = network::testing;

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
          {"Living Room PC", test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], 0},
          {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost]},
        },
        {},
      };
    });

    const network::DiscoverHostsResult result = network::discover_hosts(750U);

    ASSERT_EQ(result.hosts.size(), 2U);
    EXPECT_EQ(result.hosts[0].displayName, "Living Room PC");
    EXPECT_EQ(result.hosts[0].address, test_support::kTestIpv4Addresses[test_support::kIpLivingRoom]);
    EXPECT_EQ(result.hosts[1].displayName, "Office PC");
    EXPECT_EQ(result.hosts[1].address, test_support::kTestIpv4Addresses[test_support::kIpOffice]);
    EXPECT_EQ(result.hosts[1].port, test_support::kTestPorts[test_support::kPortDefaultHost]);
    EXPECT_TRUE(result.errorMessage.empty());
  }

  TEST(HostDiscoveryTest, UsesTheDefaultTimeoutWhenTheCallerPassesZero) {
    ScopedHostDiscoveryTestHandler guard([](uint32_t timeoutMilliseconds) {
      EXPECT_EQ(timeoutMilliseconds, 1500U);
      return network::DiscoverHostsResult {};
    });

    const network::DiscoverHostsResult result = network::discover_hosts(0U);

    EXPECT_TRUE(result.hosts.empty());
    EXPECT_TRUE(result.errorMessage.empty());
  }

  TEST(HostDiscoveryTest, TrimHostDiscoveryTextRemovesAsciiWhitespace) {
    EXPECT_EQ(host_discovery_testing::trim_host_discovery_text(" \tLiving Room PC\r\n"), "Living Room PC");
    EXPECT_TRUE(host_discovery_testing::trim_host_discovery_text("\t \r\n").empty());
  }

  TEST(HostDiscoveryTest, FirstHostDiscoveryLabelReturnsTheTrimmedLeadingDnsLabel) {
    EXPECT_EQ(host_discovery_testing::first_host_discovery_label("  Office-PC._nvstream._tcp.local  "), "Office-PC");
    EXPECT_EQ(host_discovery_testing::first_host_discovery_label("  no-dot-label  "), "no-dot-label");
  }

  TEST(HostDiscoveryTest, AppendMdnsDiscoveredHostRejectsNullResultsAndInvalidAddresses) {
    EXPECT_FALSE(host_discovery_testing::append_mdns_discovered_host(nullptr, "Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], 0));

    network::DiscoverHostsResult result {};
    EXPECT_FALSE(host_discovery_testing::append_mdns_discovered_host(&result, "Office PC", "not-an-ip", 0));
    EXPECT_TRUE(result.hosts.empty());
  }

  TEST(HostDiscoveryTest, AppendMdnsDiscoveredHostNormalizesDefaultPortsAndAddsFallbackNames) {
    network::DiscoverHostsResult result {};

    EXPECT_TRUE(host_discovery_testing::append_mdns_discovered_host(&result, {}, test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], test_support::kTestPorts[test_support::kPortResolvedHttp]));

    ASSERT_EQ(result.hosts.size(), 1U);
    EXPECT_EQ(result.hosts.front().displayName, std::string("Host ") + test_support::kTestIpv4Addresses[test_support::kIpLivingRoom]);
    EXPECT_EQ(result.hosts.front().address, test_support::kTestIpv4Addresses[test_support::kIpLivingRoom]);
    EXPECT_EQ(result.hosts.front().port, 0U);
  }

  TEST(HostDiscoveryTest, AppendMdnsDiscoveredHostTreatsDefaultAndExplicitDefaultPortsAsDuplicates) {
    network::DiscoverHostsResult result {
      {
        {"", test_support::kTestIpv4Addresses[test_support::kIpOffice], 0},
      },
      {},
    };

    EXPECT_FALSE(host_discovery_testing::append_mdns_discovered_host(&result, "Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortResolvedHttp]));

    ASSERT_EQ(result.hosts.size(), 1U);
    EXPECT_EQ(result.hosts.front().displayName, "Office PC");
    EXPECT_EQ(result.hosts.front().port, 0U);
  }

  TEST(HostDiscoveryTest, AppendMdnsDiscoveredHostKeepsDistinctCustomPorts) {
    network::DiscoverHostsResult result {};

    EXPECT_TRUE(host_discovery_testing::append_mdns_discovered_host(&result, "Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortPairing]));
    EXPECT_TRUE(host_discovery_testing::append_mdns_discovered_host(&result, "Office PC HTTPS", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortResolvedHttps]));

    ASSERT_EQ(result.hosts.size(), 2U);
    EXPECT_EQ(result.hosts[0].port, test_support::kTestPorts[test_support::kPortPairing]);
    EXPECT_EQ(result.hosts[1].port, test_support::kTestPorts[test_support::kPortResolvedHttps]);
  }

  TEST(HostDiscoveryTest, SortDiscoveredHostsUsesTheRuntimeOrderingRules) {
    std::vector<network::DiscoveredHost> hosts {
      {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortResolvedHttps]},
      {"Living Room PC", test_support::kTestIpv4Addresses[test_support::kIpLivingRoomNeighbor], 0},
      {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], test_support::kTestPorts[test_support::kPortPairing]},
      {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortPairing]},
    };

    host_discovery_testing::sort_discovered_hosts(&hosts);

    ASSERT_EQ(hosts.size(), 4U);
    EXPECT_EQ(hosts[0].displayName, "Living Room PC");
    EXPECT_EQ(hosts[1].address, test_support::kTestIpv4Addresses[test_support::kIpOffice]);
    EXPECT_EQ(hosts[1].port, test_support::kTestPorts[test_support::kPortPairing]);
    EXPECT_EQ(hosts[2].address, test_support::kTestIpv4Addresses[test_support::kIpOffice]);
    EXPECT_EQ(hosts[2].port, test_support::kTestPorts[test_support::kPortResolvedHttps]);
    EXPECT_EQ(hosts[3].address, test_support::kTestIpv4Addresses[test_support::kIpLivingRoom]);
    EXPECT_EQ(hosts[3].port, test_support::kTestPorts[test_support::kPortPairing]);
  }

  TEST(HostDiscoveryTest, SortDiscoveredHostsAcceptsANullListPointer) {
    host_discovery_testing::sort_discovered_hosts(nullptr);
  }

}  // namespace
