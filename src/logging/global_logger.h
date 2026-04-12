#pragma once

// standard includes
#include <string>

// local includes
#include "src/logging/logger.h"

namespace logging {

  /**
   * @brief Object-style facade that forwards calls to the registered process-wide logger.
   */
  class GlobalLoggerProxy {
  public:
    /**
     * @brief Return whether a process-wide logger is currently registered.
     *
     * @return true when global logging calls can emit entries.
     */
    [[nodiscard]] bool available() const;

    /**
     * @brief Return the registered process-wide logger.
     *
     * @return The registered logger, or nullptr when none is available.
     */
    [[nodiscard]] Logger *get() const;

    /**
     * @brief Record a structured entry through the registered global logger.
     *
     * @param level Severity for the entry.
     * @param category Subsystem name such as ui or streaming.
     * @param message User-visible message text.
     * @param location Source location for the entry.
     * @return true if the entry was accepted by the registered logger.
     */
    bool log(LogLevel level, std::string category, std::string message, LogSourceLocation location = LogSourceLocation::current()) const;

    /**
     * @brief Record a trace entry through the registered global logger.
     *
     * @param category Subsystem name such as ui or streaming.
     * @param message User-visible message text.
     * @param location Source location for the entry.
     * @return true if the entry was accepted by the registered logger.
     */
    bool trace(std::string category, std::string message, LogSourceLocation location = LogSourceLocation::current()) const;

    /**
     * @brief Record a debug entry through the registered global logger.
     *
     * @param category Subsystem name such as ui or streaming.
     * @param message User-visible message text.
     * @param location Source location for the entry.
     * @return true if the entry was accepted by the registered logger.
     */
    bool debug(std::string category, std::string message, LogSourceLocation location = LogSourceLocation::current()) const;

    /**
     * @brief Record an info entry through the registered global logger.
     *
     * @param category Subsystem name such as ui or streaming.
     * @param message User-visible message text.
     * @param location Source location for the entry.
     * @return true if the entry was accepted by the registered logger.
     */
    bool info(std::string category, std::string message, LogSourceLocation location = LogSourceLocation::current()) const;

    /**
     * @brief Record a warning entry through the registered global logger.
     *
     * @param category Subsystem name such as ui or streaming.
     * @param message User-visible message text.
     * @param location Source location for the entry.
     * @return true if the entry was accepted by the registered logger.
     */
    bool warn(std::string category, std::string message, LogSourceLocation location = LogSourceLocation::current()) const;

    /**
     * @brief Record an error entry through the registered global logger.
     *
     * @param category Subsystem name such as ui or streaming.
     * @param message User-visible message text.
     * @param location Source location for the entry.
     * @return true if the entry was accepted by the registered logger.
     */
    bool error(std::string category, std::string message, LogSourceLocation location = LogSourceLocation::current()) const;
  };

  /**
   * @brief Object-style facade used for convenience logging calls such as logging::logger.info(...).
   */
  extern const GlobalLoggerProxy logger;

  /**
   * @brief Register the process-wide logger used by convenience logging helpers.
   *
   * @param logger Logger instance to expose globally, or nullptr to clear it.
   */
  void set_global_logger(Logger *logger);

  /**
   * @brief Return the process-wide logger used by convenience logging helpers.
   *
   * @return The registered logger, or nullptr when none is available.
   */
  Logger *global_logger();

  /**
   * @brief Return whether a global logger is currently available.
   *
   * @return true when the convenience logging helpers can emit entries.
   */
  [[nodiscard]] bool has_global_logger();

}  // namespace logging
