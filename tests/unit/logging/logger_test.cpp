/**
 * @file tests/unit/logging/logger_test.cpp
 * @brief Verifies logging configuration and output.
 */
// test header include
#include "src/logging/logger.h"

// standard includes
#include <string>
#include <vector>

// lib includes
#include <gtest/gtest.h>

namespace {

  TEST(LoggerTest, FormatsWallClockTimestampsWithDateAndMilliseconds) {
    EXPECT_EQ(logging::format_timestamp({2026, 4, 5, 13, 7, 9, 42}), "2026-04-05 13:07:09.042");
    EXPECT_EQ(logging::format_timestamp({}), "0000-00-00 00:00:00.000");
  }

  TEST(LoggerTest, FormatsTheNoneLogLevelLabel) {
    EXPECT_STREQ(logging::to_string(logging::LogLevel::none), "NONE");
  }

  TEST(LoggerTest, FormatsSourceLocationsWithCppExtensions) {
    EXPECT_EQ(logging::format_source_location({"C:\\repo\\Moonlight-XboxOG\\src\\main.cpp", 241}), "src/main.cpp:241");
    EXPECT_EQ(logging::format_source_location({"C:\\repo\\Moonlight-XboxOG\\tests\\unit\\logging\\logger_test.cpp", 19}), "tests/unit/logging/logger_test.cpp:19");
  }

  TEST(LoggerTest, StoresEntriesAboveTheConfiguredMinimumLevel) {
    logging::Logger logger(4, []() {
      return logging::LogTimestamp {2026, 4, 5, 13, 7, 9, 42};
    });
    logger.set_minimum_level(logging::LogLevel::debug);
    logger.set_startup_debug_enabled(false);

    EXPECT_FALSE(logger.log(logging::LogLevel::trace, "streaming", "ignored"));
    EXPECT_TRUE(logger.log(logging::LogLevel::debug, "streaming", "accepted"));
    EXPECT_TRUE(logger.log(logging::LogLevel::error, "network", "failed"));

    ASSERT_EQ(logger.entries().size(), 2U);
    EXPECT_EQ(logger.entries().front().sequence, 1U);
    EXPECT_EQ(logger.entries().back().sequence, 2U);
    EXPECT_EQ(logger.entries().front().category, "streaming");
    EXPECT_EQ(logger.entries().back().message, "failed");
    EXPECT_EQ(logger.entries().front().timestamp.year, 2026);
    EXPECT_EQ(logger.entries().front().timestamp.month, 4);
    EXPECT_EQ(logger.entries().front().timestamp.day, 5);
    EXPECT_EQ(logger.entries().front().timestamp.hour, 13);
    EXPECT_EQ(logger.entries().front().timestamp.minute, 7);
    EXPECT_EQ(logger.entries().front().timestamp.second, 9);
    EXPECT_EQ(logger.entries().front().timestamp.millisecond, 42);
  }

  TEST(LoggerTest, DropsTheOldestEntriesWhenCapacityIsReached) {
    logging::Logger logger(2);
    logger.set_minimum_level(logging::LogLevel::info);
    logger.set_startup_debug_enabled(false);

    EXPECT_TRUE(logger.log(logging::LogLevel::info, "app", "first"));
    EXPECT_TRUE(logger.log(logging::LogLevel::warning, "app", "second"));
    EXPECT_TRUE(logger.log(logging::LogLevel::error, "app", "third"));

    ASSERT_EQ(logger.entries().size(), 2U);
    EXPECT_EQ(logger.entries().front().message, "second");
    EXPECT_EQ(logger.entries().back().message, "third");
  }

  TEST(LoggerTest, InvokesRegisteredSinksForAcceptedEntries) {
    logging::Logger logger;
    std::vector<std::string> seenMessages;

    logger.set_minimum_level(logging::LogLevel::info);
    logger.set_startup_debug_enabled(false);

    logger.add_sink([&seenMessages](const logging::LogEntry &entry) {
      seenMessages.push_back(logging::format_entry(entry));
    });

    const int expectedLine = __LINE__ + 1;
    EXPECT_TRUE(logger.log(logging::LogLevel::info, "ui", "opened"));

    ASSERT_EQ(seenMessages.size(), 1U);
    EXPECT_EQ(seenMessages.front(), "[INFO] [tests/unit/logging/logger_test.cpp:" + std::to_string(expectedLine) + "] ui: opened");
  }

  TEST(LoggerTest, LoggerMethodsCaptureTheCallsiteLocationWithoutMacros) {
    logging::Logger localLogger;
    localLogger.set_minimum_level(logging::LogLevel::info);
    localLogger.set_startup_debug_enabled(false);

    const int expectedLine = __LINE__ + 1;
    EXPECT_TRUE(localLogger.info("ui", "opened"));

    ASSERT_EQ(localLogger.entries().size(), 1U);
    EXPECT_EQ(localLogger.entries().front().sourceLocation.line, expectedLine);
    ASSERT_NE(localLogger.entries().front().sourceLocation.file, nullptr);
    EXPECT_NE(std::string(localLogger.entries().front().sourceLocation.file).find("logger_test.cpp"), std::string::npos);
    EXPECT_EQ(logging::format_entry(localLogger.entries().front()), "[INFO] [tests/unit/logging/logger_test.cpp:" + std::to_string(expectedLine) + "] ui: opened");
  }

  TEST(LoggerTest, NamespaceLevelGlobalLoggingCapturesTheCallsiteLocation) {
    logging::Logger localLogger;
    localLogger.set_minimum_level(logging::LogLevel::info);
    localLogger.set_startup_debug_enabled(false);
    logging::set_global_logger(&localLogger);

    const int expectedLine = __LINE__ + 1;
    EXPECT_TRUE(logging::info("ui", "opened globally"));

    ASSERT_EQ(localLogger.entries().size(), 1U);
    EXPECT_EQ(localLogger.entries().front().sourceLocation.line, expectedLine);
    ASSERT_NE(localLogger.entries().front().sourceLocation.file, nullptr);
    EXPECT_NE(std::string(localLogger.entries().front().sourceLocation.file).find("logger_test.cpp"), std::string::npos);
    logging::set_global_logger(nullptr);
  }

  TEST(LoggerTest, NamespaceLevelGlobalLoggingReturnsFalseWhenNoLoggerIsRegistered) {
    logging::set_global_logger(nullptr);

    EXPECT_FALSE(logging::info("ui", "ignored"));
  }

  TEST(LoggerTest, DispatchesTheDedicatedRuntimeFileSinkIndependentlyFromTheRetainedBufferLevel) {
    logging::Logger logger;
    std::vector<std::string> fileMessages;

    logger.set_minimum_level(logging::LogLevel::none);
    logger.set_startup_debug_enabled(false);
    logger.set_file_sink([&fileMessages](const logging::LogEntry &entry) {
      fileMessages.push_back(logging::format_entry(entry));
    });
    logger.set_file_minimum_level(logging::LogLevel::warning);

    const int expectedLine = __LINE__ + 2;
    EXPECT_FALSE(logger.log(logging::LogLevel::info, "ui", "ignored"));
    EXPECT_TRUE(logger.log(logging::LogLevel::warning, "ui", "written"));

    EXPECT_TRUE(logger.entries().empty());
    ASSERT_EQ(fileMessages.size(), 1U);
    EXPECT_EQ(fileMessages.front(), "[WARN] [tests/unit/logging/logger_test.cpp:" + std::to_string(expectedLine) + "] ui: written");
  }

  TEST(LoggerTest, SnapshotFiltersByMinimumLevel) {
    logging::Logger logger;
    logger.set_minimum_level(logging::LogLevel::trace);
    logger.set_startup_debug_enabled(false);

    EXPECT_TRUE(logger.log(logging::LogLevel::info, "ui", "info"));
    EXPECT_TRUE(logger.log(logging::LogLevel::warning, "ui", "warning"));
    EXPECT_TRUE(logger.log(logging::LogLevel::error, "ui", "error"));

    const std::vector<logging::LogEntry> snapshot = logger.snapshot(logging::LogLevel::warning);

    ASSERT_EQ(snapshot.size(), 2U);
    EXPECT_EQ(snapshot.front().level, logging::LogLevel::warning);
    EXPECT_EQ(snapshot.back().level, logging::LogLevel::error);
  }

  TEST(LoggerTest, DefaultsToSuppressingRuntimeLogsUntilALevelIsSelected) {
    logging::Logger logger;
    logger.set_startup_debug_enabled(false);

    EXPECT_EQ(logger.minimum_level(), logging::LogLevel::none);
    EXPECT_FALSE(logger.should_log(logging::LogLevel::error));
    EXPECT_FALSE(logger.log(logging::LogLevel::error, "app", "suppressed"));
    EXPECT_TRUE(logger.entries().empty());
  }

}  // namespace
