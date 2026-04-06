#include "src/startup/cover_art_cache.h"

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

  class CoverArtCacheTest : public ::testing::Test {
   protected:
    void TearDown() override {
      remove_if_present(testFilePath);
      remove_directory_if_present(testDirectory);
    }

    std::string testDirectory = "cover-art-cache-test";
    std::string cacheKey = startup::build_cover_art_cache_key("host-uuid-123", "192.168.0.10", 42);
    std::string testFilePath = testDirectory + "\\" + cacheKey + ".bin";
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

}  // namespace

