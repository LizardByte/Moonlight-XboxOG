/**
 * @file tests/unit/network/host_pairing_test.cpp
 * @brief Verifies host pairing helpers.
 */
// test header include
#include "src/network/host_pairing.h"

// standard includes
#include <atomic>

// lib includes
#include <gtest/gtest.h>

// test includes
#include "tests/support/network_test_constants.h"

namespace {

  TEST(HostPairingTest, CreatesAValidClientIdentity) {
    std::string errorMessage;
    const network::PairingIdentity identity = network::create_pairing_identity(&errorMessage);

    EXPECT_TRUE(network::is_valid_pairing_identity(identity));
    EXPECT_TRUE(errorMessage.empty());
    EXPECT_EQ(identity.uniqueId.size(), 16U);
    EXPECT_NE(identity.certificatePem.find("BEGIN CERTIFICATE"), std::string::npos);
    EXPECT_NE(identity.privateKeyPem.find("BEGIN PRIVATE KEY"), std::string::npos);
  }

  TEST(HostPairingTest, GeneratesSecureFourDigitPins) {
    for (int attempt = 0; attempt < 32; ++attempt) {
      std::string pin;
      std::string errorMessage;

      ASSERT_TRUE(network::generate_pairing_pin(&pin, &errorMessage)) << errorMessage;
      EXPECT_TRUE(errorMessage.empty());
      ASSERT_EQ(pin.size(), 4U);
      for (char character : pin) {
        EXPECT_GE(character, '0');
        EXPECT_LE(character, '9');
      }
    }
  }

  TEST(HostPairingTest, CancelledPairingReturnsImmediatelyBeforeStartingTheHandshake) {
    const network::PairingIdentity identity = network::create_pairing_identity();
    ASSERT_TRUE(network::is_valid_pairing_identity(identity));

    std::atomic cancelRequested {true};
    const network::HostPairingResult result = network::pair_host({
                                                                   test_support::kTestIpv4Addresses[test_support::kIpLivingRoom],
                                                                   test_support::kTestPorts[test_support::kPortPairing],
                                                                   "1234",
                                                                   "MoonlightXboxOG",
                                                                   identity,
                                                                 },
                                                                 &cancelRequested);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.alreadyPaired);
    EXPECT_EQ(result.message, "Pairing cancelled");
  }

  TEST(HostPairingTest, ParsesServerInfoResponsesForPairing) {
    const std::string xml =
      "<root status_code=\"200\">"
      "<hostname>Sunshine-PC</hostname>"
      "<appversion>7.1.431.0</appversion>"
      "<uuid>host-uuid-123</uuid>"
      "<LocalIP>" +
      std::string(test_support::kTestIpv4Addresses[test_support::kIpServerLocal]) +
      "</LocalIP>"
      "<ExternalIP>" +
      std::string(test_support::kTestIpv4Addresses[test_support::kIpServerExternal]) +
      "</ExternalIP>"
      "<LocalIP6>fe80::1234</LocalIP6>"
      "<MacAddress>00:11:22:33:44:55</MacAddress>"
      "<CurrentGame>42</CurrentGame>"
      "<ExternalPort>47989</ExternalPort>"
      "<HttpsPort>47990</HttpsPort>"
      "<PairStatus>1</PairStatus>"
      "</root>";

    network::HostPairingServerInfo serverInfo {};
    std::string errorMessage;

    ASSERT_TRUE(network::parse_server_info_response(xml, test_support::kTestPorts[test_support::kPortPairing], &serverInfo, &errorMessage)) << errorMessage;
    EXPECT_EQ(serverInfo.serverMajorVersion, 7);
    EXPECT_EQ(serverInfo.httpPort, test_support::kTestPorts[test_support::kPortResolvedHttp]);
    EXPECT_EQ(serverInfo.httpsPort, test_support::kTestPorts[test_support::kPortResolvedHttps]);
    EXPECT_TRUE(serverInfo.paired);
    EXPECT_EQ(serverInfo.hostName, "Sunshine-PC");
    EXPECT_EQ(serverInfo.uuid, "host-uuid-123");
    EXPECT_EQ(serverInfo.activeAddress, test_support::kTestIpv4Addresses[test_support::kIpServerLocal]);
    EXPECT_EQ(serverInfo.localAddress, test_support::kTestIpv4Addresses[test_support::kIpServerLocal]);
    EXPECT_EQ(serverInfo.remoteAddress, test_support::kTestIpv4Addresses[test_support::kIpServerExternal]);
    EXPECT_EQ(serverInfo.ipv6Address, "fe80::1234");
    EXPECT_EQ(serverInfo.macAddress, "00:11:22:33:44:55");
    EXPECT_EQ(serverInfo.runningGameId, 42U);
  }

  TEST(HostPairingTest, RejectsServerInfoResponsesMissingRequiredFields) {
    network::HostPairingServerInfo serverInfo {};
    std::string errorMessage;

    EXPECT_FALSE(network::parse_server_info_response("<root><HttpsPort>47990</HttpsPort></root>", test_support::kTestPorts[test_support::kPortPairing], &serverInfo, &errorMessage));
    EXPECT_FALSE(errorMessage.empty());
  }

  TEST(HostPairingTest, PrefersRequestedAddressForFollowUpRequests) {
    const std::string xml =
      "<root status_code=\"200\">"
      "<hostname>Sunshine-PC</hostname>"
      "<appversion>7.1.431.0</appversion>"
      "<uuid>host-uuid-123</uuid>"
      "<LocalIP>" +
      std::string(test_support::kTestIpv4Addresses[test_support::kIpLoopback]) +
      "</LocalIP>"
      "<ExternalIP>" +
      std::string(test_support::kTestIpv4Addresses[test_support::kIpExternalFallback]) +
      "</ExternalIP>"
      "<HttpsPort>47990</HttpsPort>"
      "<PairStatus>1</PairStatus>"
      "</root>";

    network::HostPairingServerInfo serverInfo {};
    std::string errorMessage;

    ASSERT_TRUE(network::parse_server_info_response(xml, test_support::kTestPorts[test_support::kPortPairing], &serverInfo, &errorMessage)) << errorMessage;
    EXPECT_EQ(serverInfo.activeAddress, test_support::kTestIpv4Addresses[test_support::kIpLoopback]);
    EXPECT_EQ(
      network::resolve_reachable_address(test_support::kTestIpv4Addresses[test_support::kIpRuntimeDhcpGateway], serverInfo),
      test_support::kTestIpv4Addresses[test_support::kIpRuntimeDhcpGateway]
    );
  }

  TEST(HostPairingTest, FallsBackToReportedAddressWhenRequestedAddressIsMissing) {
    network::HostPairingServerInfo serverInfo {};
    serverInfo.activeAddress = test_support::kTestIpv4Addresses[test_support::kIpExternalFallback];
    serverInfo.localAddress = test_support::kTestIpv4Addresses[test_support::kIpLocalFallback];
    serverInfo.remoteAddress = test_support::kTestIpv4Addresses[test_support::kIpServerExternalAlt];

    EXPECT_EQ(network::resolve_reachable_address({}, serverInfo), test_support::kTestIpv4Addresses[test_support::kIpExternalFallback]);
  }

  TEST(HostPairingTest, ParsesHostAppLists) {
    const std::string xml =
      "<root status_code=\"200\">"
      "<App><AppTitle>Steam</AppTitle><ID>101</ID><IsHdrSupported>1</IsHdrSupported></App>"
      "<App><AppTitle>Desktop</AppTitle><ID>102</ID><Hidden>0</Hidden></App>"
      "<App><AppTitle>Broken Entry</AppTitle><ID>oops</ID></App>"
      "</root>";

    std::vector<network::HostAppEntry> apps;
    std::string errorMessage;

    ASSERT_TRUE(network::parse_app_list_response(xml, &apps, &errorMessage)) << errorMessage;
    ASSERT_EQ(apps.size(), 2U);
    EXPECT_EQ(apps[0].name, "Steam");
    EXPECT_EQ(apps[0].id, 101);
    EXPECT_TRUE(apps[0].hdrSupported);
    EXPECT_EQ(apps[1].name, "Desktop");
    EXPECT_EQ(apps[1].id, 102);
    EXPECT_FALSE(apps[1].hidden);
  }

  TEST(HostPairingTest, RejectsAppListsWithoutValidApps) {
    std::vector<network::HostAppEntry> apps;
    std::string errorMessage;

    EXPECT_FALSE(network::parse_app_list_response("<root><App><AppTitle>Broken</AppTitle><ID>bad</ID></App></root>", &apps, &errorMessage));
    EXPECT_FALSE(errorMessage.empty());
  }

  TEST(HostPairingTest, ParsesAttributeBasedHostAppLists) {
    const std::string xml = R"(<root status_code="200"><App ID="201" AppTitle="Steam" IsHdrSupported="1" /><App appid="202" title="Desktop" hidden="0" /></root>)";

    std::vector<network::HostAppEntry> apps;
    std::string errorMessage;

    ASSERT_TRUE(network::parse_app_list_response(xml, &apps, &errorMessage)) << errorMessage;
    ASSERT_EQ(apps.size(), 2U);
    EXPECT_EQ(apps[0].name, "Steam");
    EXPECT_EQ(apps[0].id, 201);
    EXPECT_TRUE(apps[0].hdrSupported);
    EXPECT_EQ(apps[1].name, "Desktop");
    EXPECT_EQ(apps[1].id, 202);
    EXPECT_FALSE(apps[1].hidden);
  }

  TEST(HostPairingTest, ParsesAlternateXmlGameElementsInAppLists) {
    const std::string xml = R"(<root status_code="200"><Game><Name>Steam</Name><ID>301</ID><Hidden>false</Hidden></Game><Game title="Desktop" id="302" IsHdrSupported="true" /></root>)";

    std::vector<network::HostAppEntry> apps;
    std::string errorMessage;

    ASSERT_TRUE(network::parse_app_list_response(xml, &apps, &errorMessage)) << errorMessage;
    ASSERT_EQ(apps.size(), 2U);
    EXPECT_EQ(apps[0].name, "Steam");
    EXPECT_EQ(apps[0].id, 301);
    EXPECT_FALSE(apps[0].hidden);
    EXPECT_EQ(apps[1].name, "Desktop");
    EXPECT_EQ(apps[1].id, 302);
    EXPECT_TRUE(apps[1].hdrSupported);
  }

  TEST(HostPairingTest, RejectsNonXmlAppLists) {
    std::vector<network::HostAppEntry> apps;
    std::string errorMessage;

    EXPECT_FALSE(network::parse_app_list_response(R"({"apps":[{"name":"Steam","id":401}]})", &apps, &errorMessage));
    EXPECT_NE(errorMessage.find("not XML"), std::string::npos);
  }

  TEST(HostPairingTest, HashesEquivalentAppListsStablyAndDetectsChanges) {
    const std::vector<network::HostAppEntry> baseline = {
      {"Steam", 101, true, false},
      {"Desktop", 102, false, false},
    };
    const std::vector<network::HostAppEntry> identical = {
      {"Steam", 101, true, false},
      {"Desktop", 102, false, false},
    };
    const std::vector<network::HostAppEntry> changed = {
      {"Steam", 101, true, false},
      {"Desktop", 102, false, true},
    };

    EXPECT_EQ(network::hash_app_list_entries(baseline), network::hash_app_list_entries(identical));
    EXPECT_NE(network::hash_app_list_entries(baseline), network::hash_app_list_entries(changed));
  }

  TEST(HostPairingTest, DetectsExplicitUnpairedClientErrors) {
    EXPECT_TRUE(network::error_indicates_unpaired_client("The host reports that this client is no longer paired. Pair the host again."));
    EXPECT_TRUE(network::error_indicates_unpaired_client("The client is not authorized. Certificate verification failed."));
    EXPECT_TRUE(network::error_indicates_unpaired_client("The host returned HTTP 401 while requesting /applist"));
    EXPECT_FALSE(network::error_indicates_unpaired_client("Timed out while refreshing apps"));
  }

}  // namespace
