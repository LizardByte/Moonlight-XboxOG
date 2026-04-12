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
    none = 5,
  };

  /**
   * @brief Local wall-clock timestamp captured for each retained log entry.
   */
  struct LogTimestamp {
    int year = 0;  ///< Full calendar year in local time.
    int month = 0;  ///< One-based calendar month in local time.
    int day = 0;  ///< One-based day of month in local time.
    int hour = 0;  ///< Hour component in 24-hour local time.
    int minute = 0;  ///< Minute component in local time.
    int second = 0;  ///< Second component in local time.
    int millisecond = 0;  ///< Millisecond component in local time.
  };

  /**
   * @brief Optional source location captured for a structured log entry.
   */
  struct LogSourceLocation {
    const char *file = nullptr;  ///< Translation-unit file path where the entry originated.
    int line = 0;  ///< One-based source line number where the entry originated.

    /**
     * @brief Capture the current call-site source location when the compiler supports it.
     *
     * @param currentFile Source file reported by the compiler builtin.
     * @param currentLine Source line reported by the compiler builtin.
     * @return A source-location payload for the current call site.
     */
    [[nodiscard]] static constexpr LogSourceLocation current(
#if defined(__clang__) || defined(__GNUC__)
      const char *currentFile = __builtin_FILE(),
      int currentLine = __builtin_LINE()
#else
      const char *currentFile = nullptr,
      int currentLine = 0
#endif
    ) noexcept {
      return {currentFile, currentLine};
    }

    /**
     * @brief Return whether this source-location payload contains usable data.
     *
     * @return true when both the file path and line number are valid.
     */
    [[nodiscard]] bool valid() const {
      return file != nullptr && file[0] != '\0' && line > 0;
    }
  };

  /**
   * @brief Structured log entry stored by the in-memory logger.
   */
  struct LogEntry {
    uint64_t sequence = 0;  ///< Monotonic sequence number assigned by the logger.
    LogLevel level = LogLevel::info;  ///< Severity associated with the entry.
    std::string category;  ///< Subsystem category such as ui or network.
    std::string message;  ///< Human-readable log message.
    LogTimestamp timestamp {};  ///< Local wall-clock timestamp captured for the entry.
    LogSourceLocation sourceLocation {};  ///< Optional file-and-line source location for the entry.
  };

  /**
   * @brief Callback invoked for each accepted log entry.
   */
  using LogSink = std::function<void(const LogEntry &entry)>;

  /**
   * @brief Callback that supplies timestamps for new log entries.
   */
  using TimestampProvider = std::function<LogTimestamp()>;

  /**
   * @brief Return the display label for a log level.
   *
   * @param level The level to stringify.
   * @return A stable, uppercase label.
   */
  const char *to_string(LogLevel level);

  /**
   * @brief Format a local wall-clock timestamp for log prefixes.
   *
   * @param timestamp Local timestamp to format.
   * @return A stable YYYY-MM-DD HH:MM:SS.mmm timestamp string.
   */
  std::string format_timestamp(const LogTimestamp &timestamp);

  /**
   * @brief Format a source location for text consoles or overlays.
   *
   * @param location Source location to format.
   * @return A normalized file:line string, or an empty string when unavailable.
   */
  std::string format_source_location(const LogSourceLocation &location);

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
     * @param timestampProvider Optional timestamp callback used for new entries.
     */
    explicit Logger(std::size_t capacity = 256, TimestampProvider timestampProvider = {});

    /**
     * @brief Return the maximum number of retained entries.
     *
     * @return Maximum number of retained entries.
     */
    std::size_t capacity() const;

    /**
     * @brief Set the minimum retained in-memory log level.
     *
     * @param minimumLevel Entries below this level are not stored in the ring buffer.
     */
    void set_minimum_level(LogLevel minimumLevel);

    /**
     * @brief Return the minimum retained in-memory log level.
     *
     * @return Minimum retained level.
     */
    LogLevel minimum_level() const;

    /**
     * @brief Enable or disable pre-splash startup output through debugPrint().
     *
     * @param enabled True to mirror future log entries to the startup console.
     */
    void set_startup_debug_enabled(bool enabled);

    /**
     * @brief Return whether pre-splash startup output is currently enabled.
     *
     * @return true when future log entries are still mirrored to debugPrint().
     */
    bool startup_debug_enabled() const;

    /**
     * @brief Install or replace the runtime file sink callback.
     *
     * @param sink Callback invoked for entries accepted by the file minimum level.
     */
    void set_file_sink(LogSink sink);

    /**
     * @brief Set the minimum level written to the configured file sink.
     *
     * @param minimumLevel Entries below this level are not written to the file sink.
     */
    void set_file_minimum_level(LogLevel minimumLevel);

    /**
     * @brief Return the minimum level written to the configured file sink.
     *
     * @return Minimum accepted level for the file sink.
     */
    LogLevel file_minimum_level() const;

    /**
     * @brief Set the minimum level mirrored through DbgPrint() for xemu.
     *
     * @param minimumLevel Entries below this level are not written to the debugger console.
     */
    void set_debugger_console_minimum_level(LogLevel minimumLevel);

    /**
     * @brief Return the minimum level mirrored through DbgPrint() for xemu.
     *
     * @return Minimum accepted level for debugger-console output.
     */
    LogLevel debugger_console_minimum_level() const;

    /**
     * @brief Return whether a log level would be recorded by any enabled sink.
     *
     * @param level The candidate level.
     * @return true if the entry would be stored or dispatched.
     */
    bool should_log(LogLevel level) const;

    /**
     * @brief Record a structured entry.
     *
     * @param level Severity for the entry.
     * @param category Subsystem name such as ui or streaming.
     * @param message User-visible message text.
     * @param location Optional source location for the entry.
     * @return true if the entry was accepted.
     */
    bool log(LogLevel level, std::string category, std::string message, LogSourceLocation location = LogSourceLocation::current());

    /**
     * @brief Record a trace entry.
     *
     * @param category Subsystem name such as ui or streaming.
     * @param message User-visible message text.
     * @param location Optional source location for the entry.
     * @return true if the entry was accepted.
     */
    bool trace(std::string category, std::string message, LogSourceLocation location = LogSourceLocation::current());

    /**
     * @brief Record a debug entry.
     *
     * @param category Subsystem name such as ui or streaming.
     * @param message User-visible message text.
     * @param location Optional source location for the entry.
     * @return true if the entry was accepted.
     */
    bool debug(std::string category, std::string message, LogSourceLocation location = LogSourceLocation::current());

    /**
     * @brief Record an info entry.
     *
     * @param category Subsystem name such as ui or streaming.
     * @param message User-visible message text.
     * @param location Optional source location for the entry.
     * @return true if the entry was accepted.
     */
    bool info(std::string category, std::string message, LogSourceLocation location = LogSourceLocation::current());

    /**
     * @brief Record a warning entry.
     *
     * @param category Subsystem name such as ui or streaming.
     * @param message User-visible message text.
     * @param location Optional source location for the entry.
     * @return true if the entry was accepted.
     */
    bool warn(std::string category, std::string message, LogSourceLocation location = LogSourceLocation::current());

    /**
     * @brief Record an error entry.
     *
     * @param category Subsystem name such as ui or streaming.
     * @param message User-visible message text.
     * @param location Optional source location for the entry.
     * @return true if the entry was accepted.
     */
    bool error(std::string category, std::string message, LogSourceLocation location = LogSourceLocation::current());

    /**
     * @brief Register an observer that receives accepted entries.
     *
     * @param sink Callback invoked synchronously during logging.
     * @param minimumLevel Entries below this level are not dispatched to the sink.
     */
    void add_sink(LogSink sink, LogLevel minimumLevel = LogLevel::trace);

    /**
     * @brief Return the retained entries.
     *
     * @return Immutable view of the retained ring-buffer contents.
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
    struct RegisteredSink {
      LogLevel minimumLevel = LogLevel::trace;  ///< Minimum accepted level for the sink.
      LogSink sink;  ///< Callback invoked for matching entries.
    };

    std::size_t capacity_;
    LogLevel minimumLevel_ = LogLevel::none;
    bool startupDebugEnabled_ = true;
    LogSink fileSink_;
    LogLevel fileMinimumLevel_ = LogLevel::none;
    LogLevel debuggerConsoleMinimumLevel_ = LogLevel::none;
    uint64_t nextSequence_ = 1;
    TimestampProvider timestampProvider_;
    std::deque<LogEntry> entries_;
    std::vector<RegisteredSink> sinks_;
  };

}  // namespace logging
