#pragma once

// standard includes
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace logging {

  /**
   * @brief Severity levels used by the Moonlight client logger.
   */
  enum class LogLevel {
    trace = 0,
    debug = 1,
    info = 2,
    warning = 3,
    error = 4,
  };

  /**
   * @brief Structured log entry stored by the in-memory logger.
   */
  struct LogEntry {
    uint64_t sequence;
    LogLevel level;
    std::string category;
    std::string message;
  };

  using LogSink = std::function<void(const LogEntry &entry)>;

  /**
   * @brief Return the display label for a log level.
   *
   * @param level The level to stringify.
   * @return A stable, uppercase label.
   */
  const char *to_string(LogLevel level);

  /**
   * @brief Format a log entry for text consoles or overlays.
   *
   * @param entry The entry to format.
   * @return A formatted log line.
   */
  std::string format_entry(const LogEntry &entry);

  /**
   * @brief Small in-memory logger with a ring buffer and optional sinks.
   */
  class Logger {
   public:
    /**
     * @brief Construct a logger with the provided entry capacity.
     *
     * @param capacity Maximum number of retained entries.
     */
    explicit Logger(std::size_t capacity = 256);

    /**
     * @brief Return the maximum number of retained entries.
     */
    std::size_t capacity() const;

    /**
     * @brief Set the minimum accepted log level.
     *
     * @param minimumLevel Entries below this level are ignored.
     */
    void set_minimum_level(LogLevel minimumLevel);

    /**
     * @brief Return the minimum accepted log level.
     */
    LogLevel minimum_level() const;

    /**
     * @brief Return whether a log level would be recorded.
     *
     * @param level The candidate level.
     * @return true if the entry would be stored and dispatched.
     */
    bool should_log(LogLevel level) const;

    /**
     * @brief Record a structured entry.
     *
     * @param level Severity for the entry.
     * @param category Subsystem name such as ui or streaming.
     * @param message User-visible message text.
     * @return true if the entry was accepted.
     */
    bool log(LogLevel level, std::string category, std::string message);

    /**
     * @brief Register an observer that receives accepted entries.
     *
     * @param sink Callback invoked synchronously during logging.
     */
    void add_sink(LogSink sink);

    /**
     * @brief Return the retained entries.
     */
    const std::deque<LogEntry> &entries() const;

    /**
     * @brief Copy retained entries at or above the requested level.
     *
     * @param minimumLevel Minimum level to include in the snapshot.
     * @return Filtered log entries in insertion order.
     */
    std::vector<LogEntry> snapshot(LogLevel minimumLevel = LogLevel::trace) const;

   private:
    std::size_t capacity_;
    LogLevel minimumLevel_;
    uint64_t nextSequence_;
    std::deque<LogEntry> entries_;
    std::vector<LogSink> sinks_;
  };

}  // namespace logging
