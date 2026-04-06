#include "src/startup/client_identity_storage.h"

#include <cstdio>

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

  class ClientIdentityStorageTest: public ::testing::Test {
  protected:
    void TearDown() override {
      remove_if_present((nestedIdentityDirectory + "\\uniqueid.dat"));
      remove_if_present((nestedIdentityDirectory + "\\client.pem"));
      remove_if_present((nestedIdentityDirectory + "\\key.pem"));
      remove_directory_if_present(nestedIdentityDirectory);
      remove_directory_if_present(testDirectory + "\\nested");

      remove_if_present((testDirectory + "\\uniqueid.dat"));
      remove_if_present((testDirectory + "\\client.pem"));
      remove_if_present((testDirectory + "\\key.pem"));
      remove_directory_if_present(testDirectory);
    }

    std::string testDirectory = "pairing-storage-test";
    std::string nestedIdentityDirectory = testDirectory + "\\nested\\identity";
  };

  TEST_F(ClientIdentityStorageTest, SavesAndReloadsAClientIdentity) {
    const network::PairingIdentity identity = network::create_pairing_identity();
    ASSERT_TRUE(network::is_valid_pairing_identity(identity));

    const startup::SaveClientIdentityResult saveResult = startup::save_client_identity(identity, testDirectory);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    const startup::LoadClientIdentityResult loadResult = startup::load_client_identity(testDirectory);
    ASSERT_TRUE(loadResult.fileFound);
    EXPECT_TRUE(loadResult.warnings.empty());
    EXPECT_EQ(loadResult.identity.uniqueId, identity.uniqueId);
    EXPECT_EQ(loadResult.identity.certificatePem, identity.certificatePem);
    EXPECT_EQ(loadResult.identity.privateKeyPem, identity.privateKeyPem);
  }

  TEST_F(ClientIdentityStorageTest, MissingIdentityDirectoryReturnsNoWarnings) {
    const startup::LoadClientIdentityResult loadResult = startup::load_client_identity(testDirectory);

    EXPECT_FALSE(loadResult.fileFound);
    EXPECT_TRUE(loadResult.warnings.empty());
  }

  TEST_F(ClientIdentityStorageTest, CreatesNestedDirectoriesWhenSavingIdentity) {
    const network::PairingIdentity identity = network::create_pairing_identity();
    ASSERT_TRUE(network::is_valid_pairing_identity(identity));

    const startup::SaveClientIdentityResult saveResult = startup::save_client_identity(identity, nestedIdentityDirectory);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    const startup::LoadClientIdentityResult loadResult = startup::load_client_identity(nestedIdentityDirectory);
    EXPECT_TRUE(loadResult.fileFound);
    EXPECT_TRUE(loadResult.warnings.empty());
    EXPECT_EQ(loadResult.identity.uniqueId, identity.uniqueId);
  }

  TEST_F(ClientIdentityStorageTest, DeletesAllPersistedClientIdentityFiles) {
    const network::PairingIdentity identity = network::create_pairing_identity();
    ASSERT_TRUE(network::is_valid_pairing_identity(identity));

    const startup::SaveClientIdentityResult saveResult = startup::save_client_identity(identity, testDirectory);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    std::string errorMessage;
    EXPECT_TRUE(startup::delete_client_identity(&errorMessage, testDirectory)) << errorMessage;
    EXPECT_FALSE(std::remove((testDirectory + "\\uniqueid.dat").c_str()) == 0);
    EXPECT_FALSE(std::remove((testDirectory + "\\client.pem").c_str()) == 0);
    EXPECT_FALSE(std::remove((testDirectory + "\\key.pem").c_str()) == 0);

    const startup::LoadClientIdentityResult loadResult = startup::load_client_identity(testDirectory);
    EXPECT_FALSE(loadResult.fileFound);
    EXPECT_TRUE(loadResult.warnings.empty());
  }

  TEST_F(ClientIdentityStorageTest, DeletingMissingIdentityFilesStillSucceeds) {
    std::string errorMessage;

    EXPECT_TRUE(startup::delete_client_identity(&errorMessage, testDirectory)) << errorMessage;
    EXPECT_TRUE(errorMessage.empty());
  }

}  // namespace
