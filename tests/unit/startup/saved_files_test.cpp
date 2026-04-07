// test header include
#include "src/startup/saved_files.h"

// standard includes
#include <cstdio>
#include <vector>

// lib includes
#include <gtest/gtest.h>

// test includes
#include "tests/support/filesystem_test_utils.h"

namespace {

  void write_file_bytes(const std::string &path, const std::vector<unsigned char> &bytes) {
    FILE *file = std::fopen(path.c_str(), "wb");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(std::fwrite(bytes.data(), 1, bytes.size(), file), bytes.size());
    ASSERT_EQ(std::fclose(file), 0);
  }

  class SavedFilesTest: public ::testing::Test {
  protected:
    std::string testDirectory = "saved-files-test";
    std::string hostStoragePath = test_support::join_path(testDirectory, "moonlight-hosts.tsv");
    std::string logFilePath = test_support::join_path(testDirectory, "moonlight.log");
    std::string pairingDirectory = test_support::join_path(testDirectory, "pairing");
    std::string pairingUniqueIdPath = test_support::join_path(pairingDirectory, "uniqueid.dat");
    std::string pairingCertificatePath = test_support::join_path(pairingDirectory, "client.pem");
    std::string pairingKeyPath = test_support::join_path(pairingDirectory, "key.pem");
    std::string coverArtDirectory = test_support::join_path(testDirectory, "cover-art-cache");
    std::string coverArtFilePath = test_support::join_path(coverArtDirectory, "cover-101.bin");
    startup::SavedFileCatalogConfig config {
      hostStoragePath,
      logFilePath,
      pairingDirectory,
      coverArtDirectory,
    };

    void SetUp() override {
      ASSERT_TRUE(test_support::create_directory(testDirectory));
      ASSERT_TRUE(test_support::create_directory(pairingDirectory));
      ASSERT_TRUE(test_support::create_directory(coverArtDirectory));
    }

    void TearDown() override {
      test_support::remove_if_present(coverArtFilePath);
      test_support::remove_if_present(pairingKeyPath);
      test_support::remove_if_present(pairingCertificatePath);
      test_support::remove_if_present(pairingUniqueIdPath);
      test_support::remove_if_present(logFilePath);
      test_support::remove_if_present(hostStoragePath);
      test_support::remove_directory_if_present(coverArtDirectory);
      test_support::remove_directory_if_present(pairingDirectory);
      test_support::remove_directory_if_present(testDirectory);
    }
  };

  TEST_F(SavedFilesTest, ListsMoonlightManagedFilesThatExistOnDisk) {
    write_file_bytes(hostStoragePath, {'h', 'o', 's', 't'});
    write_file_bytes(logFilePath, {'l', 'o', 'g'});
    write_file_bytes(pairingUniqueIdPath, {'1', '2', '3', '4'});
    write_file_bytes(pairingCertificatePath, {'c', 'e', 'r', 't'});
    write_file_bytes(pairingKeyPath, {'k', 'e', 'y'});
    write_file_bytes(coverArtFilePath, {0x89, 0x50, 0x4E, 0x47});

    const startup::ListSavedFilesResult result = startup::list_saved_files(config);

    EXPECT_TRUE(result.warnings.empty());
    ASSERT_EQ(result.files.size(), 6U);
    EXPECT_EQ(result.files[0].displayName, test_support::join_path("cover-art-cache", "cover-101.bin"));
    EXPECT_EQ(result.files[1].displayName, "moonlight-hosts.tsv");
    EXPECT_EQ(result.files[2].displayName, "moonlight.log");
    EXPECT_EQ(result.files[3].displayName, test_support::join_path("pairing", "client.pem"));
    EXPECT_EQ(result.files[4].displayName, test_support::join_path("pairing", "key.pem"));
    EXPECT_EQ(result.files[5].displayName, test_support::join_path("pairing", "uniqueid.dat"));
  }

  TEST_F(SavedFilesTest, DeletesManagedSavedFiles) {
    write_file_bytes(logFilePath, {'l', 'o', 'g'});

    std::string errorMessage;
    EXPECT_TRUE(startup::delete_saved_file(logFilePath, &errorMessage, config)) << errorMessage;

    FILE *file = std::fopen(logFilePath.c_str(), "rb");
    EXPECT_EQ(file, nullptr);
  }

  TEST_F(SavedFilesTest, RefusesToDeleteFilesOutsideTheManagedStorageSet) {
    const std::string outsidePath = "outside.txt";
    write_file_bytes(outsidePath, {'x'});

    std::string errorMessage;
    EXPECT_FALSE(startup::delete_saved_file(outsidePath, &errorMessage, config));
    EXPECT_FALSE(errorMessage.empty());

    test_support::remove_if_present(outsidePath);
  }

  TEST_F(SavedFilesTest, FactoryResetDeletesAllManagedSavedFiles) {
    write_file_bytes(hostStoragePath, {'h', 'o', 's', 't'});
    write_file_bytes(logFilePath, {'l', 'o', 'g'});
    write_file_bytes(pairingUniqueIdPath, {'1', '2', '3', '4'});
    write_file_bytes(pairingCertificatePath, {'c', 'e', 'r', 't'});
    write_file_bytes(pairingKeyPath, {'k', 'e', 'y'});
    write_file_bytes(coverArtFilePath, {0x89, 0x50, 0x4E, 0x47});

    std::string errorMessage;
    EXPECT_TRUE(startup::delete_all_saved_files(&errorMessage, config)) << errorMessage;

    const startup::ListSavedFilesResult result = startup::list_saved_files(config);
    EXPECT_TRUE(result.files.empty());
  }

}  // namespace
