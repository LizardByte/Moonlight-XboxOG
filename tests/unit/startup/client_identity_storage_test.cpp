/**
 * @file tests/unit/startup/client_identity_storage_test.cpp
 * @brief Verifies client identity persistence.
 */
// test header include
#include "src/startup/client_identity_storage.h"

// standard includes
#include <cstdio>

// lib includes
#include <gtest/gtest.h>

// test includes
#include "tests/support/filesystem_test_utils.h"

namespace {

  class ClientIdentityStorageTest: public ::testing::Test {  // NOSONAR(cpp:S3656) protected members are required by gtest
  protected:
    void TearDown() override {
      test_support::remove_if_present(test_support::join_path(nestedIdentityDirectory, "uniqueid.dat"));
      test_support::remove_if_present(test_support::join_path(nestedIdentityDirectory, "client.pem"));
      test_support::remove_if_present(test_support::join_path(nestedIdentityDirectory, "key.pem"));
      test_support::remove_directory_if_present(nestedIdentityDirectory);
      test_support::remove_directory_if_present(test_support::join_path(testDirectory, "nested"));

      test_support::remove_if_present(test_support::join_path(testDirectory, "uniqueid.dat"));
      test_support::remove_if_present(test_support::join_path(testDirectory, "client.pem"));
      test_support::remove_if_present(test_support::join_path(testDirectory, "key.pem"));
      test_support::remove_directory_if_present(testDirectory);
    }

    std::string testDirectory = "pairing-storage-test";
    std::string nestedIdentityDirectory = test_support::join_path(test_support::join_path(testDirectory, "nested"), "identity");
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
    EXPECT_FALSE(std::remove(test_support::join_path(testDirectory, "uniqueid.dat").c_str()) == 0);
    EXPECT_FALSE(std::remove(test_support::join_path(testDirectory, "client.pem").c_str()) == 0);
    EXPECT_FALSE(std::remove(test_support::join_path(testDirectory, "key.pem").c_str()) == 0);

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
