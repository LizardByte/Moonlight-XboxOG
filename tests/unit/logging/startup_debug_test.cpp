// test header include
#include "src/logging/startup_debug.h"

// lib includes
#include <gtest/gtest.h>

namespace {

  TEST(StartupDebugTest, FormatsStructuredStartupConsoleLines) {
    EXPECT_STREQ(logging::startup_status_block(logging::StartupConsoleStyle::pending), "[START ]");
    EXPECT_STREQ(logging::startup_status_block(logging::StartupConsoleStyle::trace), "[TRACE ]");
    EXPECT_STREQ(logging::startup_status_block(logging::StartupConsoleStyle::error), "[ERROR ]");
    EXPECT_EQ(
      logging::format_startup_console_line(logging::StartupConsoleStyle::warning, "network", "Runtime networking is unavailable"),
      "[ WARN ] network: Runtime networking is unavailable"
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
