#include "src/startup/saved_files.h"

#include <cstdio>
#include <vector>

extern "C" {
#include <direct.h>
}

#include <gtest/gtest.h>

namespace {

  void remove_if_present(const std::string &path) {
    std::remove(path.c_str());
  }

  void remove_directory_if_present(const std::string &path) {
    _rmdir(path.c_str());
  }

  void write_file_bytes(const std::string &path, const std::vector<unsigned char> &bytes) {
    FILE *file = std::fopen(path.c_str(), "wb");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(std::fwrite(bytes.data(), 1, bytes.size(), file), bytes.size());
    ASSERT_EQ(std::fclose(file), 0);
  }

  class SavedFilesTest : public ::testing::Test {
   protected:
    std::string testDirectory = "saved-files-test";
    std::string hostStoragePath = testDirectory + "\\moonlight-hosts.tsv";
    std::string logFilePath = testDirectory + "\\moonlight.log";
    std::string pairingDirectory = testDirectory + "\\pairing";
    std::string pairingUniqueIdPath = pairingDirectory + "\\uniqueid.dat";
    std::string pairingCertificatePath = pairingDirectory + "\\client.pem";
    std::string pairingKeyPath = pairingDirectory + "\\key.pem";
    std::string coverArtDirectory = testDirectory + "\\cover-art-cache";
    std::string coverArtFilePath = coverArtDirectory + "\\cover-101.bin";
    startup::SavedFileCatalogConfig config {
      hostStoragePath,
      logFilePath,
      pairingDirectory,
      coverArtDirectory,
    };

    void SetUp() override {
      ASSERT_EQ(_mkdir(testDirectory.c_str()), 0);
      ASSERT_EQ(_mkdir(pairingDirectory.c_str()), 0);
      ASSERT_EQ(_mkdir(coverArtDirectory.c_str()), 0);
    }

    void TearDown() override {
      remove_if_present(coverArtFilePath);
      remove_if_present(pairingKeyPath);
      remove_if_present(pairingCertificatePath);
      remove_if_present(pairingUniqueIdPath);
      remove_if_present(logFilePath);
      remove_if_present(hostStoragePath);
      remove_directory_if_present(coverArtDirectory);
      remove_directory_if_present(pairingDirectory);
      remove_directory_if_present(testDirectory);
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
    EXPECT_EQ(result.files[0].displayName, "cover-art-cache\\cover-101.bin");
    EXPECT_EQ(result.files[1].displayName, "moonlight-hosts.tsv");
    EXPECT_EQ(result.files[2].displayName, "moonlight.log");
    EXPECT_EQ(result.files[3].displayName, "pairing\\client.pem");
    EXPECT_EQ(result.files[4].displayName, "pairing\\key.pem");
    EXPECT_EQ(result.files[5].displayName, "pairing\\uniqueid.dat");
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

    remove_if_present(outsidePath);
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



