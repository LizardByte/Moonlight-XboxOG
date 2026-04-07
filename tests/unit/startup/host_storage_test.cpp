// test header include
#include "src/startup/host_storage.h"

// standard includes
#include <cstdio>
#include <vector>

// lib includes
#include <gtest/gtest.h>

// test includes
#include "tests/support/filesystem_test_utils.h"

namespace {

  class HostStorageTest: public ::testing::Test {
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
      {"Living Room PC", "192.168.1.20", 0, app::PairingState::paired},
      {"Office PC", "10.0.0.25", 48000, app::PairingState::not_paired},
    };

    const startup::SaveSavedHostsResult saveResult = startup::save_saved_hosts(hosts, testFilePath);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    const startup::LoadSavedHostsResult loadResult = startup::load_saved_hosts(testFilePath);
    EXPECT_TRUE(loadResult.fileFound);
    EXPECT_TRUE(loadResult.warnings.empty());
    ASSERT_EQ(loadResult.hosts.size(), 2U);
    EXPECT_EQ(loadResult.hosts[0].displayName, "Living Room PC");
    EXPECT_EQ(loadResult.hosts[1].address, "10.0.0.25");
    EXPECT_EQ(loadResult.hosts[1].port, 48000);
  }

  TEST_F(HostStorageTest, CreatesNestedDirectoriesWhenSavingHosts) {
    const std::vector<app::HostRecord> hosts = {
      {"Living Room PC", "192.168.1.20", 0, app::PairingState::paired},
    };

    const startup::SaveSavedHostsResult saveResult = startup::save_saved_hosts(hosts, nestedFilePath);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    const startup::LoadSavedHostsResult loadResult = startup::load_saved_hosts(nestedFilePath);
    EXPECT_TRUE(loadResult.fileFound);
    ASSERT_EQ(loadResult.hosts.size(), 1U);
    EXPECT_EQ(loadResult.hosts[0].displayName, "Living Room PC");
  }

  TEST_F(HostStorageTest, SurfacesParseWarningsButKeepsValidHosts) {
    FILE *file = std::fopen(testFilePath.c_str(), "wb");
    ASSERT_NE(file, nullptr);
    const char fileContent[] =
      "Living Room PC\t192.168.1.20\t\tpaired\n"
      "Broken Host\tnot-an-ip\t\tnot_paired\n";
    ASSERT_EQ(std::fwrite(fileContent, 1, sizeof(fileContent) - 1, file), sizeof(fileContent) - 1);
    ASSERT_EQ(std::fclose(file), 0);

    const startup::LoadSavedHostsResult loadResult = startup::load_saved_hosts(testFilePath);
    EXPECT_TRUE(loadResult.fileFound);
    ASSERT_EQ(loadResult.hosts.size(), 1U);
    EXPECT_EQ(loadResult.hosts[0].address, "192.168.1.20");
    ASSERT_EQ(loadResult.warnings.size(), 1U);
    EXPECT_NE(loadResult.warnings[0].find("Line 2"), std::string::npos);
  }

}  // namespace
