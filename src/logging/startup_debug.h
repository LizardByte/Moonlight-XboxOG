#pragma once

// standard includes
#include <string>
#include <string_view>

// local includes
#include "src/logging/logger.h"

namespace logging {

  /**
   * @brief Fixed-width startup console prefixes used by the pre-splash debug wrapper.
   */
  enum class StartupConsoleStyle {
    pending,
    trace,
    debug,
    info,
    warning,
    error,
  };

  /**
   * @brief Return the fixed-width status block shown at the start of startup console lines.
   *
   * @param style Startup console style to stringify.
   * @return Fixed-width status block text.
   */
  [[nodiscard]] const char *startup_status_block(StartupConsoleStyle style);

  /**
   * @brief Format one startup console line without writing it to the debug console.
   *
   * @param style Debian-style token shown for the line.
   * @param category Short subsystem category such as startup or sdl.
   * @param message Human-readable console text.
   * @return Formatted startup console line without a trailing newline.
   */
  [[nodiscard]] std::string format_startup_console_line(StartupConsoleStyle style, std::string_view category, std::string_view message);

  /**
   * @brief Enable or disable startup console output.
   *
   * @param enabled True to allow future startup console writes.
   */
  void set_startup_console_enabled(bool enabled);

  /**
   * @brief Return whether startup console output is currently enabled.
   *
   * @return true when pre-splash console lines should still be emitted.
   */
  [[nodiscard]] bool startup_console_enabled();

  /**
   * @brief Print one Debian-style startup console line when output is enabled.
   *
   * @param style Debian-style token shown for the line.
   * @param category Short subsystem category such as startup or sdl.
   * @param message Human-readable console text.
   */
  void print_startup_console_line(StartupConsoleStyle style, std::string_view category, std::string_view message);

  /**
   * @brief Print one structured log-level startup console line when output is enabled.
   *
   * @param level Structured log level to map into a console token.
   * @param category Short subsystem category such as startup or sdl.
   * @param message Human-readable console text.
   */
  void print_startup_log(LogLevel level, std::string_view category, std::string_view message);

}  // namespace logging
