/**
 * @file tests/unit/logging/log_file_test.cpp
 * @brief Verifies log file lifecycle helpers.
 */
// test header include
#include "src/logging/log_file.h"

// standard includes
#include <cstdio>
#include <string>

// lib includes
#include <gtest/gtest.h>

// test includes
#include "tests/support/filesystem_test_utils.h"

namespace {

  void write_raw_text_file(const std::string &path, const std::string &content) {
    FILE *file = std::fopen(path.c_str(), "wb");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(std::fwrite(content.data(), 1, content.size(), file), content.size());
    ASSERT_EQ(std::fclose(file), 0);
  }

  std::string test_log_file_path(const char *name) {
    return test_support::join_path(test_support::join_path("test-output", "logging"), name);
  }

  TEST(LogFileTest, AppendsEntriesAndLoadsRecentLines) {
    const std::string filePath = test_log_file_path("moonlight.log");
    std::remove(filePath.c_str());

    ASSERT_TRUE(logging::append_log_file_entry({1, logging::LogLevel::info, "app", "first", {2026, 4, 5, 13, 0, 1, 234}}, filePath));
    ASSERT_TRUE(logging::append_log_file_entry({2, logging::LogLevel::warning, "net", "second", {2026, 4, 5, 13, 0, 2, 345}}, filePath));
    ASSERT_TRUE(logging::append_log_file_entry({3, logging::LogLevel::error, "ui", "third", {2026, 4, 5, 13, 0, 3, 456}}, filePath));

    const logging::LoadLogFileResult loadedLog = logging::load_log_file(filePath, 2U);
    ASSERT_TRUE(loadedLog.fileFound);
    EXPECT_TRUE(loadedLog.errorMessage.empty());
    ASSERT_EQ(loadedLog.lines.size(), 2U);
    EXPECT_EQ(loadedLog.lines[0], "[2026-04-05 13:00:02.345] [WARN] net: second");
    EXPECT_EQ(loadedLog.lines[1], "[2026-04-05 13:00:03.456] [ERROR] ui: third");
  }

  TEST(LogFileTest, MissingFilesReturnNoError) {
    const std::string filePath = test_log_file_path("missing.log");
    std::remove(filePath.c_str());

    const logging::LoadLogFileResult loadedLog = logging::load_log_file(filePath, 8U);
    EXPECT_FALSE(loadedLog.fileFound);
    EXPECT_TRUE(loadedLog.lines.empty());
    EXPECT_TRUE(loadedLog.errorMessage.empty());
  }

  TEST(LogFileTest, ResetLogFileTruncatesExistingContents) {
    const std::string filePath = test_log_file_path("reset.log");
    std::remove(filePath.c_str());

    ASSERT_TRUE(logging::append_log_file_entry({1, logging::LogLevel::info, "app", "first", {2026, 4, 5, 13, 0, 1, 0}}, filePath));
    ASSERT_TRUE(logging::append_log_file_entry({2, logging::LogLevel::warning, "ui", "second", {2026, 4, 5, 13, 0, 2, 0}}, filePath));
    ASSERT_TRUE(logging::reset_log_file(filePath));
    ASSERT_TRUE(logging::append_log_file_entry({3, logging::LogLevel::error, "net", "fresh", {2026, 4, 5, 13, 0, 3, 0}}, filePath));

    const logging::LoadLogFileResult loadedLog = logging::load_log_file(filePath, 0U);
    ASSERT_TRUE(loadedLog.fileFound);
    ASSERT_EQ(loadedLog.lines.size(), 1U);
    EXPECT_EQ(loadedLog.lines.front(), "[2026-04-05 13:00:03.000] [ERROR] net: fresh");
  }

  TEST(LogFileTest, LoadsLongPhysicalLinesWithoutSplittingThemIntoFragments) {
    const std::string filePath = test_log_file_path("long-lines.log");
    std::remove(filePath.c_str());

    const std::string longMessage(1600U, 'x');
    ASSERT_TRUE(logging::append_log_file_entry({1, logging::LogLevel::info, "app", longMessage, {2026, 4, 5, 13, 0, 43, 210}}, filePath));

    const logging::LoadLogFileResult loadedLog = logging::load_log_file(filePath, 0U);
    ASSERT_TRUE(loadedLog.fileFound);
    ASSERT_EQ(loadedLog.lines.size(), 1U);
    EXPECT_EQ(loadedLog.lines.front(), std::string("[2026-04-05 13:00:43.210] [INFO] app: ") + longMessage);
  }

  TEST(LogFileTest, RuntimeLogFileSinkCanResetTheTargetFileExplicitly) {
    const std::string filePath = test_log_file_path("runtime-reset.log");
    std::remove(filePath.c_str());

    ASSERT_TRUE(logging::append_log_file_entry({1, logging::LogLevel::warning, "app", "stale", {2026, 4, 5, 13, 0, 1, 0}}, filePath));

    const logging::RuntimeLogFileSink sink(filePath);
    ASSERT_TRUE(sink.reset());

    const logging::LoadLogFileResult loadedLog = logging::load_log_file(filePath, 0U);
    ASSERT_TRUE(loadedLog.fileFound);
    EXPECT_TRUE(loadedLog.lines.empty());
  }

  TEST(LogFileTest, RuntimeLogFileSinkAppendsAcceptedEntriesToItsConfiguredFile) {
    const std::string filePath = test_log_file_path("runtime-sink.log");
    std::remove(filePath.c_str());

    logging::RuntimeLogFileSink sink(filePath);
    ASSERT_TRUE(sink.consume({2, logging::LogLevel::info, "app", "fresh", {2026, 4, 5, 13, 0, 2, 0}}));

    const logging::LoadLogFileResult loadedLog = logging::load_log_file(filePath, 0U);
    ASSERT_TRUE(loadedLog.fileFound);
    ASSERT_EQ(loadedLog.lines.size(), 1U);
    EXPECT_EQ(loadedLog.lines.front(), "[2026-04-05 13:00:02.000] [INFO] app: fresh");
  }

  TEST(LogFileTest, PersistsSourceLocationsWhenEntriesProvideThem) {
    const std::string filePath = test_log_file_path("source-location.log");
    std::remove(filePath.c_str());

    ASSERT_TRUE(logging::append_log_file_entry({4, logging::LogLevel::info, "app", "with location", {2026, 4, 5, 13, 0, 4, 0}, {"C:\\repo\\Moonlight-XboxOG\\src\\main.cpp", 241}}, filePath));

    const logging::LoadLogFileResult loadedLog = logging::load_log_file(filePath, 0U);
    ASSERT_TRUE(loadedLog.fileFound);
    ASSERT_EQ(loadedLog.lines.size(), 1U);
    EXPECT_EQ(loadedLog.lines.front(), "[2026-04-05 13:00:04.000] [INFO] [src/main.cpp:241] app: with location");
  }

  TEST(LogFileTest, LoadsTheFinalLineEvenWhenTheFileDoesNotEndWithANewline) {
    const std::string filePath = test_log_file_path("unterminated.log");
    std::remove(filePath.c_str());
    ASSERT_TRUE(test_support::create_directory(test_support::join_path("test-output", "logging")));
    write_raw_text_file(filePath, "first line\r\nsecond line\r\nthird line");

    const logging::LoadLogFileResult loadedLog = logging::load_log_file(filePath, 0U);

    ASSERT_TRUE(loadedLog.fileFound);
    ASSERT_EQ(loadedLog.lines.size(), 3U);
    EXPECT_EQ(loadedLog.lines.back(), "third line");
  }

  TEST(LogFileTest, RuntimeLogFileSinkExposesItsConfiguredPath) {
    const std::string filePath = test_log_file_path("accessor.log");
    const logging::RuntimeLogFileSink sink(filePath);

    EXPECT_EQ(sink.file_path(), filePath);
  }

}  // namespace
