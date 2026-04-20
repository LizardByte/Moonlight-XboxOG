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
      VIDEO_MODE {1280, 720, 32, 60},
      true,
      24,
      2500,
      true,
      true,
    };

    const app::SaveAppSettingsResult saveResult = app::save_app_settings(savedSettings, settingsPath);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    const app::LoadAppSettingsResult loadResult = app::load_app_settings(settingsPath);
    EXPECT_TRUE(loadResult.fileFound);
    EXPECT_TRUE(loadResult.warnings.empty());
    EXPECT_EQ(loadResult.settings.loggingLevel, logging::LogLevel::debug);
    EXPECT_EQ(loadResult.settings.xemuConsoleLoggingLevel, logging::LogLevel::warning);
    EXPECT_EQ(loadResult.settings.logViewerPlacement, app::LogViewerPlacement::left);
    EXPECT_TRUE(loadResult.settings.preferredVideoModeSet);
    EXPECT_EQ(loadResult.settings.preferredVideoMode.width, 1280);
    EXPECT_EQ(loadResult.settings.preferredVideoMode.height, 720);
    EXPECT_EQ(loadResult.settings.preferredVideoMode.refresh, 60);
    EXPECT_EQ(loadResult.settings.streamFramerate, 24);
    EXPECT_EQ(loadResult.settings.streamBitrateKbps, 2500);
    EXPECT_TRUE(loadResult.settings.playAudioOnPc);
    EXPECT_TRUE(loadResult.settings.showPerformanceStats);
    EXPECT_FALSE(loadResult.cleanupRequired);
  }

  TEST_F(SettingsStorageTest, SavesAndLoadsLowStreamResolutionPresets) {
    const app::AppSettings savedSettings {
      logging::LogLevel::none,
      logging::LogLevel::none,
      app::LogViewerPlacement::full,
      VIDEO_MODE {352, 240, 32, 60},
      true,
      15,
      500,
      false,
      false,
    };

    const app::SaveAppSettingsResult saveResult = app::save_app_settings(savedSettings, settingsPath);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;

    const app::LoadAppSettingsResult loadResult = app::load_app_settings(settingsPath);
    EXPECT_TRUE(loadResult.fileFound);
    EXPECT_TRUE(loadResult.warnings.empty());
    EXPECT_TRUE(loadResult.settings.preferredVideoModeSet);
    EXPECT_EQ(loadResult.settings.preferredVideoMode.width, 352);
    EXPECT_EQ(loadResult.settings.preferredVideoMode.height, 240);
    EXPECT_EQ(loadResult.settings.preferredVideoMode.bpp, 32);
    EXPECT_EQ(loadResult.settings.preferredVideoMode.refresh, 60);
    EXPECT_EQ(loadResult.settings.streamFramerate, 15);
    EXPECT_EQ(loadResult.settings.streamBitrateKbps, 500);
  }

  TEST_F(SettingsStorageTest, MissingFilesReturnDefaultsWithoutWarnings) {
    const app::LoadAppSettingsResult loadResult = app::load_app_settings(settingsPath);

    EXPECT_FALSE(loadResult.fileFound);
    EXPECT_TRUE(loadResult.warnings.empty());
    EXPECT_FALSE(loadResult.cleanupRequired);
    EXPECT_EQ(loadResult.settings.loggingLevel, logging::LogLevel::none);
    EXPECT_EQ(loadResult.settings.xemuConsoleLoggingLevel, logging::LogLevel::none);
    EXPECT_EQ(loadResult.settings.logViewerPlacement, app::LogViewerPlacement::full);
    EXPECT_FALSE(loadResult.settings.preferredVideoModeSet);
    EXPECT_EQ(loadResult.settings.streamFramerate, 20);
    EXPECT_EQ(loadResult.settings.streamBitrateKbps, 1500);
    EXPECT_FALSE(loadResult.settings.playAudioOnPc);
    EXPECT_FALSE(loadResult.settings.showPerformanceStats);
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
      "log_viewer_placement = \"top\"\n\n"
      "[streaming]\n"
      "video_width = \"wide\"\n"
      "fps = \"fast\"\n"
      "play_audio_on_pc = \"sometimes\"\n"
    );

    const app::LoadAppSettingsResult loadResult = app::load_app_settings(settingsPath);
    EXPECT_TRUE(loadResult.fileFound);
    EXPECT_GE(loadResult.warnings.size(), 6U);
    EXPECT_TRUE(loadResult.cleanupRequired);
    EXPECT_EQ(loadResult.settings.loggingLevel, logging::LogLevel::none);
    EXPECT_EQ(loadResult.settings.xemuConsoleLoggingLevel, logging::LogLevel::none);
    EXPECT_EQ(loadResult.settings.logViewerPlacement, app::LogViewerPlacement::full);
    EXPECT_FALSE(loadResult.settings.preferredVideoModeSet);
    EXPECT_EQ(loadResult.settings.streamFramerate, 20);
    EXPECT_FALSE(loadResult.settings.playAudioOnPc);
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

  TEST_F(SettingsStorageTest, AcceptsMixedCaseLoggingValuesAndWarnAlias) {
    write_text_file(
      settingsPath,
      "[logging]\n"
      "file_minimum_level = \"DeBuG\"\n"
      "xemu_console_minimum_level = \"WARN\"\n\n"
      "[ui]\n"
      "log_viewer_placement = \"RIGHT\"\n"
    );

    const app::LoadAppSettingsResult loadResult = app::load_app_settings(settingsPath);

    EXPECT_TRUE(loadResult.fileFound);
    EXPECT_TRUE(loadResult.warnings.empty());
    EXPECT_EQ(loadResult.settings.loggingLevel, logging::LogLevel::debug);
    EXPECT_EQ(loadResult.settings.xemuConsoleLoggingLevel, logging::LogLevel::warning);
    EXPECT_EQ(loadResult.settings.logViewerPlacement, app::LogViewerPlacement::right);
  }

  TEST_F(SettingsStorageTest, MarksUnknownKeysAndLegacySectionsForCleanup) {
    write_text_file(
      settingsPath,
      "[logging]\n"
      "file_minimum_level = \"info\"\n"
      "obsolete_key = true\n\n"
      "[ui]\n"
      "log_viewer_placement = \"left\"\n\n"
      "[streaming]\n"
      "fps = 30\n"
      "obsolete_key = true\n"
      "theme = \"green\"\n\n"
      "[debug]\n"
      "startup_console_enabled = true\n\n"
      "[other]\n"
      "value = 1\n"
    );

    const app::LoadAppSettingsResult loadResult = app::load_app_settings(settingsPath);

    EXPECT_TRUE(loadResult.fileFound);
    EXPECT_TRUE(loadResult.cleanupRequired);
    EXPECT_GE(loadResult.warnings.size(), 5U);
    EXPECT_EQ(loadResult.settings.loggingLevel, logging::LogLevel::info);
    EXPECT_EQ(loadResult.settings.logViewerPlacement, app::LogViewerPlacement::left);
    EXPECT_EQ(loadResult.settings.streamFramerate, 30);
  }

  TEST_F(SettingsStorageTest, ReportsParseAndTypeErrorsAsWarnings) {
    write_text_file(
      settingsPath,
      "[logging]\n"
      "file_minimum_level = 7\n"
      "xemu_console_minimum_level = false\n\n"
      "[ui]\n"
      "log_viewer_placement = 42\n\n"
      "[streaming]\n"
      "show_performance_stats = \"sometimes\"\n"
    );

    app::LoadAppSettingsResult loadResult = app::load_app_settings(settingsPath);
    EXPECT_TRUE(loadResult.fileFound);
    EXPECT_GE(loadResult.warnings.size(), 4U);
    EXPECT_EQ(loadResult.settings.loggingLevel, logging::LogLevel::none);
    EXPECT_EQ(loadResult.settings.xemuConsoleLoggingLevel, logging::LogLevel::none);
    EXPECT_EQ(loadResult.settings.logViewerPlacement, app::LogViewerPlacement::full);
    EXPECT_FALSE(loadResult.settings.showPerformanceStats);

    write_text_file(settingsPath, "[logging\nfile_minimum_level = \"info\"\n");
    loadResult = app::load_app_settings(settingsPath);
    EXPECT_TRUE(loadResult.fileFound);
    ASSERT_FALSE(loadResult.warnings.empty());
    EXPECT_NE(loadResult.warnings.front().find("Failed to parse settings file"), std::string::npos);
  }

}  // namespace
