// test header include
#include "src/startup/cover_art_cache.h"

// standard includes
#include <cstdio>
#include <vector>

// lib includes
#include <gtest/gtest.h>

// test includes
#include "tests/support/filesystem_test_utils.h"

namespace {

  class CoverArtCacheTest: public ::testing::Test {
  protected:
    void TearDown() override {
      test_support::remove_if_present(testFilePath);
      test_support::remove_directory_if_present(testDirectory);
    }

    std::string testDirectory = "cover-art-cache-test";
    std::string cacheKey = startup::build_cover_art_cache_key("host-uuid-123", "192.168.0.10", 42);
    std::string testFilePath = test_support::join_path(testDirectory, cacheKey + ".bin");
  };

  TEST_F(CoverArtCacheTest, SavesAndReloadsCachedCoverArtBytes) {
    const std::vector<unsigned char> bytes = {0x89, 0x50, 0x4E, 0x47, 0x10, 0x20, 0x30};

    const startup::SaveCoverArtResult saveResult = startup::save_cover_art(cacheKey, bytes, testDirectory);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;
    EXPECT_TRUE(startup::cover_art_exists(cacheKey, testDirectory));

    const startup::LoadCoverArtResult loadResult = startup::load_cover_art(cacheKey, testDirectory);
    EXPECT_TRUE(loadResult.fileFound);
    EXPECT_TRUE(loadResult.errorMessage.empty());
    EXPECT_EQ(loadResult.bytes, bytes);
  }

  TEST_F(CoverArtCacheTest, HandlesMissingEntriesWithoutErrors) {
    const startup::LoadCoverArtResult loadResult = startup::load_cover_art(cacheKey, testDirectory);

    EXPECT_FALSE(loadResult.fileFound);
    EXPECT_TRUE(loadResult.bytes.empty());
    EXPECT_TRUE(loadResult.errorMessage.empty());
    EXPECT_FALSE(startup::cover_art_exists(cacheKey, testDirectory));
  }

  TEST_F(CoverArtCacheTest, DeletesAnExistingCachedCoverArtEntry) {
    const std::vector<unsigned char> bytes = {0x89, 0x50, 0x4E, 0x47, 0x10, 0x20, 0x30};

    const startup::SaveCoverArtResult saveResult = startup::save_cover_art(cacheKey, bytes, testDirectory);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;
    ASSERT_TRUE(startup::cover_art_exists(cacheKey, testDirectory));

    std::string errorMessage;
    EXPECT_TRUE(startup::delete_cover_art(cacheKey, &errorMessage, testDirectory)) << errorMessage;
    EXPECT_FALSE(startup::cover_art_exists(cacheKey, testDirectory));

    const startup::LoadCoverArtResult loadResult = startup::load_cover_art(cacheKey, testDirectory);
    EXPECT_FALSE(loadResult.fileFound);
    EXPECT_TRUE(loadResult.errorMessage.empty());
  }

  TEST_F(CoverArtCacheTest, DeletingAMissingCachedCoverArtEntryStillSucceeds) {
    std::string errorMessage;

    EXPECT_TRUE(startup::delete_cover_art(cacheKey, &errorMessage, testDirectory)) << errorMessage;
    EXPECT_TRUE(errorMessage.empty());
  }

}  // namespace
