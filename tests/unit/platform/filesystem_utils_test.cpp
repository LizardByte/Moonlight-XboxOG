/**
 * @file tests/unit/platform/filesystem_utils_test.cpp
 * @brief Verifies filesystem utility helpers.
 */
// test header include
#include "src/platform/filesystem_utils.h"

// standard includes
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

// lib includes
#include <gtest/gtest.h>

// test includes
#include "tests/support/filesystem_test_utils.h"

namespace {

  void write_test_file(const std::string &path, const std::string &content) {
    FILE *file = std::fopen(path.c_str(), "wb");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(std::fwrite(content.data(), 1, content.size(), file), content.size());
    ASSERT_EQ(std::fclose(file), 0);
  }

  class FilesystemUtilsTest: public ::testing::Test {  // NOSONAR(cpp:S3656) protected members are required by gtest
  protected:
    std::string rootDirectory = "filesystem-utils-test";
    std::string nestedDirectory = test_support::join_path(test_support::join_path(rootDirectory, "level-one"), "level-two");
    std::string filePath = test_support::join_path(nestedDirectory, "payload.bin");

    void TearDown() override {
      test_support::remove_tree_if_present(rootDirectory);
    }
  };

  TEST_F(FilesystemUtilsTest, JoinsPathsAndFindsParentAndFileNames) {
    EXPECT_EQ(platform::join_path({}, "child.txt"), "child.txt");
    EXPECT_EQ(platform::join_path("parent", {}), "parent");
    EXPECT_EQ(platform::join_path("parent", "child.txt"), test_support::join_path("parent", "child.txt"));
    EXPECT_EQ(platform::join_path(test_support::join_path("parent", "nested"), "child.txt"), test_support::join_path(test_support::join_path("parent", "nested"), "child.txt"));
    EXPECT_EQ(platform::parent_directory(filePath), nestedDirectory);
    EXPECT_EQ(platform::parent_directory("payload.bin"), "");
    EXPECT_EQ(platform::file_name_from_path(filePath), "payload.bin");
    EXPECT_EQ(platform::file_name_from_path("payload.bin"), "payload.bin");
  }

  TEST_F(FilesystemUtilsTest, EnsuresNestedDirectoriesExistAndTreatsEmptyPathsAsNoOp) {
    std::string errorMessage;

    EXPECT_TRUE(platform::ensure_directory_exists({}, &errorMessage)) << errorMessage;
    EXPECT_TRUE(platform::ensure_directory_exists(nestedDirectory, &errorMessage)) << errorMessage;
    EXPECT_TRUE(std::filesystem::is_directory(nestedDirectory));
    EXPECT_TRUE(platform::ensure_directory_exists(nestedDirectory, &errorMessage)) << errorMessage;
  }

  TEST_F(FilesystemUtilsTest, ReportsRegularFileSizesAndRejectsDirectoriesOrMissingPaths) {
    ASSERT_TRUE(platform::ensure_directory_exists(nestedDirectory, nullptr));
    write_test_file(filePath, "payload");

    std::uint64_t sizeBytes = 0;
    EXPECT_TRUE(platform::try_get_file_size(filePath, &sizeBytes));
    EXPECT_EQ(sizeBytes, 7U);
    EXPECT_TRUE(platform::try_get_file_size(filePath, nullptr));
    EXPECT_FALSE(platform::try_get_file_size(nestedDirectory, &sizeBytes));
    EXPECT_FALSE(platform::try_get_file_size(test_support::join_path(rootDirectory, "missing.bin"), &sizeBytes));
  }

  TEST_F(FilesystemUtilsTest, ComparesPrefixesUsingPlatformPathRules) {
    const std::string prefix = test_support::join_path(rootDirectory, "level-one");
    const std::string matchingPath = test_support::join_path(prefix, "child.txt");

    EXPECT_TRUE(platform::path_has_prefix(matchingPath, prefix));
    EXPECT_FALSE(platform::path_has_prefix(matchingPath, {}));
    EXPECT_FALSE(platform::path_has_prefix(prefix, matchingPath));
#if defined(_WIN32) || defined(NXDK)
    EXPECT_TRUE(platform::path_has_prefix("C:\\Moonlight\\Data", "c:\\moonlight"));
#else
    EXPECT_FALSE(platform::path_has_prefix("/Moonlight/Data", "/moonlight"));
#endif
  }

}  // namespace
