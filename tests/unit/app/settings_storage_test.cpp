/**
 * @file tests/unit/app/settings_storage_test.cpp
 * @brief Verifies application settings persistence.
 */
// test header include
#include "src/app/settings_storage.h"

// standard includes
#include <cstdio>
#include <string>
#include <string_view>

// lib includes
#include <gtest/gtest.h>

// test includes
#include "tests/support/filesystem_test_utils.h"

namespace {

  void write_text_file(const std::string &path, std::string_view content) {
    FILE *file = std::fopen(path.c_str(), "wb");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(std::fwrite(content.data(), 1, content.size(), file), content.size());
    ASSERT_EQ(std::fclose(file), 0);
  }

  class SettingsStorageTest: public ::testing::Test {  // NOSONAR(cpp:S3656) protected members are required by gtest
  protected:
    std::string testDirectory = "settings-storage-test";
    std::string settingsPath = test_support::join_path(testDirectory, "moonlight.toml");

    void SetUp() override {
      ASSERT_TRUE(test_support::create_directory(testDirectory));
    }

    void TearDown() override {
      test_support::remove_if_present(settingsPath);
      test_support::remove_directory_if_present(testDirectory);
    }
  };

  TEST_F(SettingsStorageTest, SavesAndLoadsTomlSettings) {
    const app::AppSettings savedSettings {
      logging::LogLevel::debug,
      logging::LogLevel::warning,
      app::LogViewerPlacement::left,
    };

    const app::SaveAppSettingsResult saveResult = app::save_app_settings(savedSettings, settingsPath);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    const app::LoadAppSettingsResult loadResult = app::load_app_settings(settingsPath);
    EXPECT_TRUE(loadResult.fileFound);
    EXPECT_TRUE(loadResult.warnings.empty());
    EXPECT_EQ(loadResult.settings.loggingLevel, logging::LogLevel::debug);
    EXPECT_EQ(loadResult.settings.xemuConsoleLoggingLevel, logging::LogLevel::warning);
    EXPECT_EQ(loadResult.settings.logViewerPlacement, app::LogViewerPlacement::left);
    EXPECT_FALSE(loadResult.cleanupRequired);
  }

  TEST_F(SettingsStorageTest, InvalidValuesFallBackToDefaultsWithWarnings) {
    write_text_file(
      settingsPath,
      "[logging]\n"
      "file_minimum_level = \"loud\"\n"
      "xemu_console_minimum_level = \"chatty\"\n\n"
      "[debug]\n"
      "startup_console_enabled = \"sometimes\"\n\n"
      "[ui]\n"
      "log_viewer_placement = \"top\"\n"
    );

    const app::LoadAppSettingsResult loadResult = app::load_app_settings(settingsPath);
    EXPECT_TRUE(loadResult.fileFound);
    EXPECT_GE(loadResult.warnings.size(), 3U);
    EXPECT_TRUE(loadResult.cleanupRequired);
    EXPECT_EQ(loadResult.settings.loggingLevel, logging::LogLevel::none);
    EXPECT_EQ(loadResult.settings.xemuConsoleLoggingLevel, logging::LogLevel::none);
    EXPECT_EQ(loadResult.settings.logViewerPlacement, app::LogViewerPlacement::full);
  }

  TEST_F(SettingsStorageTest, LegacyLoggingKeyLoadsAndRequestsCleanup) {
    write_text_file(
      settingsPath,
      "[logging]\n"
      "minimum_level = \"error\"\n\n"
      "[ui]\n"
      "log_viewer_placement = \"right\"\n"
    );

    const app::LoadAppSettingsResult loadResult = app::load_app_settings(settingsPath);

    EXPECT_TRUE(loadResult.fileFound);
    EXPECT_TRUE(loadResult.cleanupRequired);
    EXPECT_EQ(loadResult.settings.loggingLevel, logging::LogLevel::error);
    EXPECT_EQ(loadResult.settings.xemuConsoleLoggingLevel, logging::LogLevel::none);
    EXPECT_EQ(loadResult.settings.logViewerPlacement, app::LogViewerPlacement::right);
  }

}  // namespace
