/**
 * @file tests/unit/logging/startup_debug_test.cpp
 * @brief Verifies startup debug logging.
 */
// test header include
#include "src/logging/logger.h"

// lib includes
#include <gtest/gtest.h>

namespace {

  TEST(StartupDebugTest, FormatsStructuredStartupConsoleLines) {
    EXPECT_EQ(
      logging::format_startup_console_line(logging::LogLevel::warning, "network", "Runtime networking is unavailable"),
      "[WARN] network: Runtime networking is unavailable"
    );
    EXPECT_EQ(
      logging::format_startup_console_line(logging::LogLevel::info, "memory", "Total physical memory: 64 MiB (16384 pages)"),
      "[INFO] memory: Total physical memory: 64 MiB (16384 pages)"
    );
  }

  TEST(StartupDebugTest, CanDisableStartupConsoleEmissionGlobally) {
    logging::set_startup_console_enabled(true);
    EXPECT_TRUE(logging::startup_console_enabled());

    logging::set_startup_console_enabled(false);
    EXPECT_FALSE(logging::startup_console_enabled());

    logging::set_startup_console_enabled(true);
  }

}  // namespace
