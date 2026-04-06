// class header include
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

  TEST(LoggerTest, StoresEntriesAboveTheConfiguredMinimumLevel) {
    logging::Logger logger(4, []() {
      return logging::LogTimestamp {2026, 4, 5, 13, 7, 9, 42};
    });
    logger.set_minimum_level(logging::LogLevel::debug);

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

    logger.add_sink([&seenMessages](const logging::LogEntry &entry) {
      seenMessages.push_back(logging::format_entry(entry));
    });

    EXPECT_TRUE(logger.log(logging::LogLevel::info, "ui", "opened"));

    ASSERT_EQ(seenMessages.size(), 1U);
    EXPECT_EQ(seenMessages.front(), "[INFO] ui: opened");
  }

  TEST(LoggerTest, SnapshotFiltersByMinimumLevel) {
    logging::Logger logger;

    EXPECT_TRUE(logger.log(logging::LogLevel::info, "ui", "info"));
    EXPECT_TRUE(logger.log(logging::LogLevel::warning, "ui", "warning"));
    EXPECT_TRUE(logger.log(logging::LogLevel::error, "ui", "error"));

    const std::vector<logging::LogEntry> snapshot = logger.snapshot(logging::LogLevel::warning);

    ASSERT_EQ(snapshot.size(), 2U);
    EXPECT_EQ(snapshot.front().level, logging::LogLevel::warning);
    EXPECT_EQ(snapshot.back().level, logging::LogLevel::error);
  }

}  // namespace
