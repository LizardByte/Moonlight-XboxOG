// class header include
#include "src/logging/startup_debug.h"

// standard includes
#include <string>

// nxdk includes
#include <hal/debug.h>

namespace {

  bool g_startupConsoleEnabled = true;

  logging::StartupConsoleStyle startup_console_style_for_level(logging::LogLevel level) {
    switch (level) {
      case logging::LogLevel::trace:
        return logging::StartupConsoleStyle::trace;
      case logging::LogLevel::debug:
        return logging::StartupConsoleStyle::debug;
      case logging::LogLevel::info:
      case logging::LogLevel::none:
        return logging::StartupConsoleStyle::info;
      case logging::LogLevel::warning:
        return logging::StartupConsoleStyle::warning;
      case logging::LogLevel::error:
        return logging::StartupConsoleStyle::error;
    }

    return logging::StartupConsoleStyle::info;
  }

}  // namespace

namespace logging {

  const char *startup_status_block(StartupConsoleStyle style) {
    switch (style) {
      case StartupConsoleStyle::pending:
        return "[START ]";
      case StartupConsoleStyle::trace:
        return "[TRACE ]";
      case StartupConsoleStyle::debug:
        return "[DEBUG ]";
      case StartupConsoleStyle::info:
        return "[ INFO ]";
      case StartupConsoleStyle::warning:
        return "[ WARN ]";
      case StartupConsoleStyle::error:
        return "[ERROR ]";
    }

    return "[ INFO ]";
  }

  std::string format_startup_console_line(StartupConsoleStyle style, std::string_view category, std::string_view message) {
    std::string line = startup_status_block(style);
    line.push_back(' ');
    if (!category.empty()) {
      line.append(category.data(), category.size());
      line.append(": ");
    }
    line.append(message.data(), message.size());
    return line;
  }

  void set_startup_console_enabled(bool enabled) {
    g_startupConsoleEnabled = enabled;
  }

  bool startup_console_enabled() {
    return g_startupConsoleEnabled;
  }

  void print_startup_console_line(StartupConsoleStyle style, std::string_view category, std::string_view message) {
    if (!startup_console_enabled()) {
      return;
    }

    const std::string line = format_startup_console_line(style, category, message);
    debugPrint("%s\n", line.c_str());
  }

  void print_startup_log(LogLevel level, std::string_view category, std::string_view message) {
    print_startup_console_line(startup_console_style_for_level(level), category, message);
  }

}  // namespace logging
