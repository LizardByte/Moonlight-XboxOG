/**
 * @file tests/unit/app/host_records_test.cpp
 * @brief Verifies host record models and utilities.
 */
// class header include
#include "src/app/host_records.h"

// standard includes
#include <vector>

// lib includes
#include <gtest/gtest.h>

// test includes
#include "tests/support/network_test_constants.h"

namespace {

  TEST(HostRecordsTest, NormalizesAndValidatesIpv4Addresses) {
    EXPECT_EQ(app::normalize_ipv4_address("192.168.001.010"), "192.168.1.10");
    EXPECT_TRUE(app::is_valid_ipv4_address("10.0.0.5"));
    EXPECT_FALSE(app::is_valid_ipv4_address("256.0.0.1"));
    EXPECT_FALSE(app::is_valid_ipv4_address("10.0.0"));
  }

  TEST(HostRecordsTest, ValidatesRecordsBeforeTheyAreSaved) {
    const app::HostRecord validRecord {
      "Living Room PC",
      test_support::kTestIpv4Addresses[test_support::kIpLivingRoom],
      0,
      app::PairingState::not_paired,
    };
    std::string errorMessage;

    EXPECT_TRUE(app::validate_host_record(validRecord, &errorMessage));
    EXPECT_TRUE(errorMessage.empty());

    const app::HostRecord invalidRecord {
      "",
      "999.168.1.20",
      0,
      app::PairingState::not_paired,
    };
    EXPECT_FALSE(app::validate_host_record(invalidRecord, &errorMessage));
    EXPECT_FALSE(errorMessage.empty());
  }

  TEST(HostRecordsTest, RejectsDisplayNamesWithTabsAndNonNormalizedAddresses) {
    std::string errorMessage;

    EXPECT_FALSE(app::validate_host_record({"Living\tRoom", test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], 0, app::PairingState::paired}, &errorMessage));
    EXPECT_NE(errorMessage.find("tabs or new lines"), std::string::npos);

    errorMessage.clear();
    EXPECT_FALSE(app::validate_host_record({"Living Room", "192.168.001.010", 0, app::PairingState::paired}, &errorMessage));
    EXPECT_NE(errorMessage.find("already be normalized"), std::string::npos);
  }

  TEST(HostRecordsTest, SerializesAndParsesRoundTripHostLists) {
    const std::vector<app::HostRecord> records = {
      {"Living Room PC", test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], 0, app::PairingState::paired},
      {"Steam Deck Dock", test_support::kTestIpv4Addresses[test_support::kIpSteamDeckDock], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::not_paired},
    };

    const std::string serializedRecords = app::serialize_host_records(records);
    const app::ParseHostRecordsResult parsedRecords = app::parse_host_records(serializedRecords);

    ASSERT_TRUE(parsedRecords.errors.empty());
    ASSERT_EQ(parsedRecords.records.size(), 2U);
    EXPECT_EQ(parsedRecords.records[0].displayName, "Living Room PC");
    EXPECT_EQ(parsedRecords.records[0].address, test_support::kTestIpv4Addresses[test_support::kIpLivingRoom]);
    EXPECT_EQ(parsedRecords.records[0].port, 0);
    EXPECT_EQ(parsedRecords.records[0].pairingState, app::PairingState::paired);
    EXPECT_EQ(parsedRecords.records[1].displayName, "Steam Deck Dock");
    EXPECT_EQ(parsedRecords.records[1].address, test_support::kTestIpv4Addresses[test_support::kIpSteamDeckDock]);
    EXPECT_EQ(parsedRecords.records[1].port, test_support::kTestPorts[test_support::kPortDefaultHost]);
    EXPECT_EQ(parsedRecords.records[1].pairingState, app::PairingState::not_paired);
  }

  TEST(HostRecordsTest, RoundTripsCachedAppListMetadata) {
    app::HostRecord host {
      "Office PC",
      test_support::kTestIpv4Addresses[test_support::kIpOffice],
      test_support::kTestPorts[test_support::kPortDefaultHost],
      app::PairingState::paired,
    };
    host.runningGameId = 102U;
    host.resolvedHttpPort = test_support::kTestPorts[test_support::kPortResolvedHttp];
    host.httpsPort = test_support::kTestPorts[test_support::kPortResolvedHttps];
    host.appListContentHash = 0xABCD1234ULL;
    host.apps = {
      {"Steam Big Picture", 101, true, false, true, "steam-big-picture", true, false},
      {"Desktop", 102, false, true, false, "desktop-cover", false, false},
    };

    const app::ParseHostRecordsResult parsedRecords = app::parse_host_records(app::serialize_host_records({host}));

    ASSERT_TRUE(parsedRecords.errors.empty());
    ASSERT_EQ(parsedRecords.records.size(), 1U);
    ASSERT_EQ(parsedRecords.records.front().apps.size(), 2U);
    EXPECT_EQ(parsedRecords.records.front().runningGameId, 102U);
    EXPECT_EQ(parsedRecords.records.front().resolvedHttpPort, test_support::kTestPorts[test_support::kPortResolvedHttp]);
    EXPECT_EQ(parsedRecords.records.front().httpsPort, test_support::kTestPorts[test_support::kPortResolvedHttps]);
    EXPECT_EQ(parsedRecords.records.front().appListContentHash, 0xABCD1234ULL);
    EXPECT_EQ(parsedRecords.records.front().appListState, app::HostAppListState::ready);
    EXPECT_EQ(parsedRecords.records.front().apps[0].name, "Steam Big Picture");
    EXPECT_TRUE(parsedRecords.records.front().apps[0].favorite);
    EXPECT_TRUE(parsedRecords.records.front().apps[0].boxArtCached);
    EXPECT_FALSE(parsedRecords.records.front().apps[0].running);
    EXPECT_TRUE(parsedRecords.records.front().apps[1].hidden);
    EXPECT_TRUE(parsedRecords.records.front().apps[1].running);
  }

  TEST(HostRecordsTest, PercentEncodesCachedAppFieldsThatContainSeparators) {
    app::HostRecord host {
      "Office PC",
      test_support::kTestIpv4Addresses[test_support::kIpOffice],
      test_support::kTestPorts[test_support::kPortDefaultHost],
      app::PairingState::paired,
    };
    host.apps = {
      {"Steam, Desktop|HDR", 101, true, true, true, "cover/key,101|hdr", true, false},
    };

    const std::string serialized = app::serialize_host_records({host});
    const app::ParseHostRecordsResult parsedRecords = app::parse_host_records(serialized);

    ASSERT_TRUE(parsedRecords.errors.empty());
    ASSERT_EQ(parsedRecords.records.size(), 1U);
    ASSERT_EQ(parsedRecords.records.front().apps.size(), 1U);
    EXPECT_EQ(parsedRecords.records.front().apps.front().name, "Steam, Desktop|HDR");
    EXPECT_EQ(parsedRecords.records.front().apps.front().boxArtCacheKey, "cover/key,101|hdr");
    EXPECT_TRUE(parsedRecords.records.front().apps.front().hidden);
    EXPECT_TRUE(parsedRecords.records.front().apps.front().favorite);
    EXPECT_TRUE(parsedRecords.records.front().apps.front().boxArtCached);
  }

  TEST(HostRecordsTest, ReportsMalformedSerializedLinesWithoutDroppingValidRecords) {
    const std::string serializedRecords =
      "Living Room PC\t192.168.1.20\t\tpaired\t0,0,0,0\t\n"
      "Broken Host\tnot-an-ip\t\tnot_paired\t0,0,0,0\t\n"
      "Bad Format\n"
      "Office PC\t10.0.0.25\t48000\tnot_paired\t0,0,0,0\t\n";

    const app::ParseHostRecordsResult parsedRecords = app::parse_host_records(serializedRecords);

    ASSERT_EQ(parsedRecords.records.size(), 2U);
    EXPECT_EQ(parsedRecords.records[0].address, test_support::kTestIpv4Addresses[test_support::kIpLivingRoom]);
    EXPECT_EQ(parsedRecords.records[0].port, 0);
    EXPECT_EQ(parsedRecords.records[1].address, test_support::kTestIpv4Addresses[test_support::kIpOffice]);
    EXPECT_EQ(parsedRecords.records[1].port, test_support::kTestPorts[test_support::kPortDefaultHost]);
    ASSERT_EQ(parsedRecords.errors.size(), 2U);
    EXPECT_NE(parsedRecords.errors[0].find("Line 2"), std::string::npos);
    EXPECT_NE(parsedRecords.errors[1].find("Line 3"), std::string::npos);
  }

  TEST(HostRecordsTest, DetectsDuplicateSavedAddresses) {
    const std::vector<app::HostRecord> records = {
      {"Living Room PC", test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], 0, app::PairingState::paired},
    };

    EXPECT_TRUE(app::contains_host_address(records, test_support::kTestIpv4Addresses[test_support::kIpLivingRoom]));
    EXPECT_FALSE(app::contains_host_address(records, test_support::kTestIpv4Addresses[test_support::kIpLivingRoomNeighbor]));
    EXPECT_FALSE(app::contains_host_address(records, test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], test_support::kTestPorts[test_support::kPortDefaultHost]));
  }

  TEST(HostRecordsTest, MatchesHostEndpointsAgainstResolvedHttpAndHttpsPorts) {
    app::HostRecord host {
      "Office PC",
      test_support::kTestIpv4Addresses[test_support::kIpOffice],
      test_support::kTestPorts[test_support::kPortDefaultHost],
      app::PairingState::paired,
    };
    host.resolvedHttpPort = test_support::kTestPorts[test_support::kPortResolvedHttp];
    host.httpsPort = test_support::kTestPorts[test_support::kPortResolvedHttps];

    EXPECT_TRUE(app::host_matches_endpoint(host, test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost]));
    EXPECT_TRUE(app::host_matches_endpoint(host, test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortResolvedHttp]));
    EXPECT_TRUE(app::host_matches_endpoint(host, test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortResolvedHttps]));
    EXPECT_FALSE(app::host_matches_endpoint(host, test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortPairing]));
  }

  TEST(HostRecordsTest, ParsesPortOverridesAndFallsBackToTheDefaultPort) {
    uint16_t parsedPort = 0;

    EXPECT_TRUE(app::try_parse_host_port({}, &parsedPort));
    EXPECT_EQ(parsedPort, 0);
    EXPECT_EQ(app::effective_host_port(parsedPort), app::DEFAULT_HOST_PORT);

    EXPECT_TRUE(app::try_parse_host_port("48000", &parsedPort));
    EXPECT_EQ(parsedPort, test_support::kTestPorts[test_support::kPortDefaultHost]);
    EXPECT_EQ(app::effective_host_port(parsedPort), test_support::kTestPorts[test_support::kPortDefaultHost]);

    EXPECT_FALSE(app::try_parse_host_port("0", &parsedPort));
    EXPECT_FALSE(app::try_parse_host_port("70000", &parsedPort));
    EXPECT_FALSE(app::try_parse_host_port("47a89", &parsedPort));
  }

  TEST(HostRecordsTest, ReturnsStableEnumNames) {
    EXPECT_STREQ(app::to_string(app::PairingState::not_paired), "not_paired");
    EXPECT_STREQ(app::to_string(app::PairingState::paired), "paired");
    EXPECT_STREQ(app::to_string(app::HostReachability::unknown), "unknown");
    EXPECT_STREQ(app::to_string(app::HostReachability::online), "online");
    EXPECT_STREQ(app::to_string(app::HostReachability::offline), "offline");
  }

}  // namespace
