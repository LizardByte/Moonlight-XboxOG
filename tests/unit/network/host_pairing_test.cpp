#include "src/network/host_pairing.h"

#include <atomic>
#include <gtest/gtest.h>

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

  TEST(HostPairingTest, CancelledPairingReturnsImmediatelyBeforeStartingTheHandshake) {
    const network::PairingIdentity identity = network::create_pairing_identity();
    ASSERT_TRUE(network::is_valid_pairing_identity(identity));

    std::atomic<bool> cancelRequested = true;
    const network::HostPairingResult result = network::pair_host({
                                                                   "192.168.1.20",
                                                                   47984,
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
      "<LocalIP>192.168.1.25</LocalIP>"
      "<ExternalIP>203.0.113.7</ExternalIP>"
      "<LocalIP6>fe80::1234</LocalIP6>"
      "<MacAddress>00:11:22:33:44:55</MacAddress>"
      "<CurrentGame>42</CurrentGame>"
      "<ExternalPort>47989</ExternalPort>"
      "<HttpsPort>47990</HttpsPort>"
      "<PairStatus>1</PairStatus>"
      "</root>";

    network::HostPairingServerInfo serverInfo {};
    std::string errorMessage;

    ASSERT_TRUE(network::parse_server_info_response(xml, 47984, &serverInfo, &errorMessage)) << errorMessage;
    EXPECT_EQ(serverInfo.serverMajorVersion, 7);
    EXPECT_EQ(serverInfo.httpPort, 47989);
    EXPECT_EQ(serverInfo.httpsPort, 47990);
    EXPECT_TRUE(serverInfo.paired);
    EXPECT_EQ(serverInfo.hostName, "Sunshine-PC");
    EXPECT_EQ(serverInfo.uuid, "host-uuid-123");
    EXPECT_EQ(serverInfo.activeAddress, "192.168.1.25");
    EXPECT_EQ(serverInfo.localAddress, "192.168.1.25");
    EXPECT_EQ(serverInfo.remoteAddress, "203.0.113.7");
    EXPECT_EQ(serverInfo.ipv6Address, "fe80::1234");
    EXPECT_EQ(serverInfo.macAddress, "00:11:22:33:44:55");
    EXPECT_EQ(serverInfo.runningGameId, 42U);
  }

  TEST(HostPairingTest, RejectsServerInfoResponsesMissingRequiredFields) {
    network::HostPairingServerInfo serverInfo {};
    std::string errorMessage;

    EXPECT_FALSE(network::parse_server_info_response("<root><HttpsPort>47990</HttpsPort></root>", 47984, &serverInfo, &errorMessage));
    EXPECT_FALSE(errorMessage.empty());
  }

  TEST(HostPairingTest, PrefersRequestedAddressForFollowUpRequests) {
    const std::string xml =
      "<root status_code=\"200\">"
      "<hostname>Sunshine-PC</hostname>"
      "<appversion>7.1.431.0</appversion>"
      "<uuid>host-uuid-123</uuid>"
      "<LocalIP>127.0.0.1</LocalIP>"
      "<ExternalIP>192.168.0.50</ExternalIP>"
      "<HttpsPort>47990</HttpsPort>"
      "<PairStatus>1</PairStatus>"
      "</root>";

    network::HostPairingServerInfo serverInfo {};
    std::string errorMessage;

    ASSERT_TRUE(network::parse_server_info_response(xml, 47984, &serverInfo, &errorMessage)) << errorMessage;
    EXPECT_EQ(serverInfo.activeAddress, "127.0.0.1");
    EXPECT_EQ(network::resolve_reachable_address("10.0.2.2", serverInfo), "10.0.2.2");
  }

  TEST(HostPairingTest, FallsBackToReportedAddressWhenRequestedAddressIsMissing) {
    network::HostPairingServerInfo serverInfo {};
    serverInfo.activeAddress = "192.168.0.50";
    serverInfo.localAddress = "192.168.0.51";
    serverInfo.remoteAddress = "203.0.113.9";

    EXPECT_EQ(network::resolve_reachable_address({}, serverInfo), "192.168.0.50");
  }

  TEST(HostPairingTest, ParsesSunshineAppLists) {
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

  TEST(HostPairingTest, ParsesAttributeBasedSunshineAppLists) {
    const std::string xml =
      "<root status_code=\"200\">"
      "<App ID=\"201\" AppTitle=\"Steam\" IsHdrSupported=\"1\" />"
      "<App appid=\"202\" title=\"Desktop\" hidden=\"0\" />"
      "</root>";

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
    const std::string xml =
      "<root status_code=\"200\">"
      "<Game><Name>Steam</Name><ID>301</ID><Hidden>false</Hidden></Game>"
      "<Game title=\"Desktop\" id=\"302\" IsHdrSupported=\"true\" />"
      "</root>";

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

  TEST(HostPairingTest, ParsesJsonAppLists) {
    const std::string json =
      "{"
      "\"apps\":["
      "{\"name\":\"Steam\",\"id\":401,\"hdrSupported\":true},"
      "{\"title\":\"Desktop\",\"appid\":\"402\",\"hidden\":false}"
      "]"
      "}";

    std::vector<network::HostAppEntry> apps;
    std::string errorMessage;

    ASSERT_TRUE(network::parse_app_list_response(json, &apps, &errorMessage)) << errorMessage;
    ASSERT_EQ(apps.size(), 2U);
    EXPECT_EQ(apps[0].name, "Steam");
    EXPECT_EQ(apps[0].id, 401);
    EXPECT_TRUE(apps[0].hdrSupported);
    EXPECT_EQ(apps[1].name, "Desktop");
    EXPECT_EQ(apps[1].id, 402);
    EXPECT_FALSE(apps[1].hidden);
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
    EXPECT_TRUE(network::error_indicates_unpaired_client("The host reports that this client is no longer paired. Pair the host again from Sunshine."));
    EXPECT_TRUE(network::error_indicates_unpaired_client("The client is not authorized. Certificate verification failed."));
    EXPECT_TRUE(network::error_indicates_unpaired_client("The host returned HTTP 401 while requesting /applist"));
    EXPECT_FALSE(network::error_indicates_unpaired_client("Timed out while refreshing apps"));
  }

}  // namespace
