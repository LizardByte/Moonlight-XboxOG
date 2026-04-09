// test header include
#include "src/startup/host_storage.h"

// standard includes
#include <cstdio>
#include <string>
#include <vector>

// lib includes
#include <gtest/gtest.h>

// test includes
#include "tests/support/filesystem_test_utils.h"
#include "tests/support/network_test_constants.h"

namespace {

  class HostStorageTest: public ::testing::Test {  // NOSONAR(cpp:S3656) protected members are required by gtest
  protected:
    void TearDown() override {
      test_support::remove_if_present(nestedFilePath);
      test_support::remove_directory_if_present(test_support::join_path(testDirectory, "nested"));
      test_support::remove_if_present(testFilePath);
      test_support::remove_directory_if_present(testDirectory);
    }

    std::string testDirectory = "host-storage-test";
    std::string testFilePath = "host-storage-test.tsv";
    std::string nestedFilePath = test_support::join_path(test_support::join_path(testDirectory, "nested"), "hosts.tsv");
  };

  TEST_F(HostStorageTest, LoadsMissingFilesWithoutWarnings) {
    const startup::LoadSavedHostsResult result = startup::load_saved_hosts(testFilePath);

    EXPECT_FALSE(result.fileFound);
    EXPECT_TRUE(result.hosts.empty());
    EXPECT_TRUE(result.warnings.empty());
  }

  TEST_F(HostStorageTest, SavesAndReloadsHostRecords) {
    const std::vector<app::HostRecord> hosts = {
      {"Living Room PC", test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], 0, app::PairingState::paired},
      {"Office PC", test_support::kTestIpv4Addresses[test_support::kIpOffice], test_support::kTestPorts[test_support::kPortDefaultHost], app::PairingState::not_paired},
    };

    const startup::SaveSavedHostsResult saveResult = startup::save_saved_hosts(hosts, testFilePath);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    const startup::LoadSavedHostsResult loadResult = startup::load_saved_hosts(testFilePath);
    EXPECT_TRUE(loadResult.fileFound);
    EXPECT_TRUE(loadResult.warnings.empty());
    ASSERT_EQ(loadResult.hosts.size(), 2U);
    EXPECT_EQ(loadResult.hosts[0].displayName, "Living Room PC");
    EXPECT_EQ(loadResult.hosts[1].address, test_support::kTestIpv4Addresses[test_support::kIpOffice]);
    EXPECT_EQ(loadResult.hosts[1].port, test_support::kTestPorts[test_support::kPortDefaultHost]);
  }

  TEST_F(HostStorageTest, CreatesNestedDirectoriesWhenSavingHosts) {
    const std::vector<app::HostRecord> hosts = {
      {"Living Room PC", test_support::kTestIpv4Addresses[test_support::kIpLivingRoom], 0, app::PairingState::paired},
    };

    const startup::SaveSavedHostsResult saveResult = startup::save_saved_hosts(hosts, nestedFilePath);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    const startup::LoadSavedHostsResult loadResult = startup::load_saved_hosts(nestedFilePath);
    EXPECT_TRUE(loadResult.fileFound);
    ASSERT_EQ(loadResult.hosts.size(), 1U);
    EXPECT_EQ(loadResult.hosts[0].displayName, "Living Room PC");
  }

  TEST_F(HostStorageTest, SavesAndReloadsCachedAppLists) {
    app::HostRecord host {
      "Office PC",
      test_support::kTestIpv4Addresses[test_support::kIpOffice],
      test_support::kTestPorts[test_support::kPortDefaultHost],
      app::PairingState::paired,
    };
    host.runningGameId = 101U;
    host.resolvedHttpPort = test_support::kTestPorts[test_support::kPortResolvedHttp];
    host.httpsPort = test_support::kTestPorts[test_support::kPortResolvedHttps];
    host.appListContentHash = 0x99887766ULL;
    host.apps = {
      {"Steam", 101, true, false, true, "steam-cover", true, false},
      {"Desktop", 102, false, true, false, "desktop-cover", false, false},
    };

    const startup::SaveSavedHostsResult saveResult = startup::save_saved_hosts({host}, testFilePath);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    const startup::LoadSavedHostsResult loadResult = startup::load_saved_hosts(testFilePath);
    ASSERT_TRUE(loadResult.warnings.empty());
    ASSERT_EQ(loadResult.hosts.size(), 1U);
    ASSERT_EQ(loadResult.hosts[0].apps.size(), 2U);
    EXPECT_EQ(loadResult.hosts[0].appListContentHash, 0x99887766ULL);
    EXPECT_EQ(loadResult.hosts[0].resolvedHttpPort, test_support::kTestPorts[test_support::kPortResolvedHttp]);
    EXPECT_EQ(loadResult.hosts[0].httpsPort, test_support::kTestPorts[test_support::kPortResolvedHttps]);
    EXPECT_TRUE(loadResult.hosts[0].apps[0].favorite);
    EXPECT_TRUE(loadResult.hosts[0].apps[0].running);
    EXPECT_TRUE(loadResult.hosts[0].apps[1].hidden);
  }

  TEST_F(HostStorageTest, SurfacesParseWarningsButKeepsValidHosts) {
    FILE *file = std::fopen(testFilePath.c_str(), "wb");
    ASSERT_NE(file, nullptr);
    const std::string fileContent =
      "Living Room PC\t" + std::string(test_support::kTestIpv4Addresses[test_support::kIpLivingRoom]) +
      "\t\tpaired\t0,0,0,0\t\n"
      "Broken Host\tnot-an-ip\t\tnot_paired\t0,0,0,0\t\n";
    ASSERT_EQ(std::fwrite(fileContent.data(), 1, fileContent.size(), file), fileContent.size());
    ASSERT_EQ(std::fclose(file), 0);

    const startup::LoadSavedHostsResult loadResult = startup::load_saved_hosts(testFilePath);
    EXPECT_TRUE(loadResult.fileFound);
    ASSERT_EQ(loadResult.hosts.size(), 1U);
    EXPECT_EQ(loadResult.hosts[0].address, test_support::kTestIpv4Addresses[test_support::kIpLivingRoom]);
    ASSERT_EQ(loadResult.warnings.size(), 1U);
    EXPECT_NE(loadResult.warnings[0].find("Line 2"), std::string::npos);
  }

}  // namespace
