// class header include
#include "src/logging/logger.h"

// standard includes
#include <string>
#include <vector>

// lib includes
#include <gtest/gtest.h>

namespace {

  TEST(LoggerTest, StoresEntriesAboveTheConfiguredMinimumLevel) {
    logging::Logger logger(4);
    logger.set_minimum_level(logging::LogLevel::debug);

    EXPECT_FALSE(logger.log(logging::LogLevel::trace, "streaming", "ignored"));
    EXPECT_TRUE(logger.log(logging::LogLevel::debug, "streaming", "accepted"));
    EXPECT_TRUE(logger.log(logging::LogLevel::error, "network", "failed"));

    ASSERT_EQ(logger.entries().size(), 2U);
    EXPECT_EQ(logger.entries().front().sequence, 1U);
    EXPECT_EQ(logger.entries().back().sequence, 2U);
    EXPECT_EQ(logger.entries().front().category, "streaming");
    EXPECT_EQ(logger.entries().back().message, "failed");
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
