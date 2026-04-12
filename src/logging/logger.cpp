// class header include
#include "src/logging/logger.h"

// standard includes
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>
#include <utility>

#if defined(_WIN32)
  #include <windows.h>  // NOSONAR(cpp:S3806) nxdk requires lowercase header names
#endif

#if defined(NXDK)
  #include <hal/debug.h>
  #include <xboxkrnl/xboxkrnl.h>
#endif

namespace {

  logging::Logger *&global_logger_slot() {
    static logging::Logger *globalLogger = nullptr;
    return globalLogger;
  }

  bool &startup_console_enabled_slot() {
    static bool startupConsoleEnabled = true;
    return startupConsoleEnabled;
  }

  bool is_enabled(logging::LogLevel candidateLevel, logging::LogLevel minimumLevel) {
    return static_cast<int>(candidateLevel) >= static_cast<int>(minimumLevel);
  }

  logging::Logger *registered_logger() {
    return global_logger_slot();
  }

  logging::LogLevel startup_console_level(logging::LogLevel level) {
    return level == logging::LogLevel::none ? logging::LogLevel::info : level;
  }

  std::string normalized_source_path(const char *filePath) {
    // NOTE: do not trim file extensions, it's needed so IDE can link the file and line number
    if (filePath == nullptr || filePath[0] == '\0') {
      return {};
    }

    std::string normalized(filePath);
    for (char &character : normalized) {
      if (character == '\\') {
        character = '/';
      }
    }

    for (const char *marker : {"src/", "tests/"}) {
      if (const std::size_t markerOffset = normalized.find(marker); markerOffset != std::string::npos) {
        return normalized.substr(markerOffset);
      }
    }

    if (const std::size_t lastSeparator = normalized.find_last_of('/'); lastSeparator != std::string::npos) {
      return normalized.substr(lastSeparator + 1U);
    }

    return normalized;
  }

  std::string debugger_console_line(const logging::LogEntry &entry) {
    return std::string("[") + logging::format_timestamp(entry.timestamp) + "] " + logging::format_entry(entry);
  }

  void emit_debugger_console_line(const logging::LogEntry &entry) {
#if defined(NXDK)
    const std::string line = debugger_console_line(entry);
    DbgPrint("%s\r\n", line.c_str());
#else
    (void) entry;
#endif
  }

  logging::LogTimestamp current_local_timestamp() {
#if defined(_WIN32)
    SYSTEMTIME localTime {};
    GetLocalTime(&localTime);
    return {
      static_cast<int>(localTime.wYear),
      static_cast<int>(localTime.wMonth),
      static_cast<int>(localTime.wDay),
      static_cast<int>(localTime.wHour),
      static_cast<int>(localTime.wMinute),
      static_cast<int>(localTime.wSecond),
      static_cast<int>(localTime.wMilliseconds),
    };
#else
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime {};
  #if defined(_MSC_VER)
    localtime_s(&localTime, &nowTime);
  #else
    localtime_r(&nowTime, &localTime);
  #endif
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    return {
      localTime.tm_year + 1900,
      localTime.tm_mon + 1,
      localTime.tm_mday,
      localTime.tm_hour,
      localTime.tm_min,
      localTime.tm_sec,
      static_cast<int>(milliseconds.count()),
    };
#endif
  }

  bool is_valid_timestamp(const logging::LogTimestamp &timestamp) {
    return timestamp.year > 0 && timestamp.month >= 1 && timestamp.month <= 12 && timestamp.day >= 1 && timestamp.day <= 31 && timestamp.hour >= 0 && timestamp.hour <= 23 && timestamp.minute >= 0 && timestamp.minute <= 59 && timestamp.second >= 0 && timestamp.second <= 60 && timestamp.millisecond >= 0 && timestamp.millisecond <= 999;
  }

}  // namespace

namespace logging {

  const char *to_string(LogLevel level) {
    switch (level) {
      case LogLevel::trace:
        return "TRACE";
      case LogLevel::debug:
        return "DEBUG";
      case LogLevel::info:
        return "INFO";
      case LogLevel::warning:
        return "WARN";
      case LogLevel::error:
        return "ERROR";
      case LogLevel::none:
        return "NONE";
    }

    return "UNKNOWN";
  }

  std::string format_timestamp(const LogTimestamp &timestamp) {
    std::array<char, 32> buffer {};
    const bool validTimestamp = is_valid_timestamp(timestamp);
    std::snprintf(
      buffer.data(),
      buffer.size(),
      "%04d-%02d-%02d %02d:%02d:%02d.%03d",
      validTimestamp ? timestamp.year : 0,
      validTimestamp ? timestamp.month : 0,
      validTimestamp ? timestamp.day : 0,
      validTimestamp ? timestamp.hour : 0,
      validTimestamp ? timestamp.minute : 0,
      validTimestamp ? timestamp.second : 0,
      validTimestamp ? timestamp.millisecond : 0
    );
    return {buffer.data()};
  }

  std::string format_source_location(const LogSourceLocation &location) {
    if (!location.valid()) {
      return {};
    }

    const std::string normalizedPath = normalized_source_path(location.file);
    if (normalizedPath.empty()) {
      return {};
    }

    return normalizedPath + ":" + std::to_string(location.line);
  }

  std::string format_entry(const LogEntry &entry) {
    const std::string sourceLocationText = format_source_location(entry.sourceLocation);
    std::string line = std::string("[") + to_string(entry.level) + "] ";
    if (!sourceLocationText.empty()) {
      line += "[" + sourceLocationText + "] ";
      if (!entry.category.empty()) {
        line += entry.category + ": ";
      }
      line += entry.message;
      return line;
    }

    if (!entry.category.empty()) {
      line += entry.category + ": ";
    }
    line += entry.message;
    return line;
  }

  void set_global_logger(Logger *logger) {
    global_logger_slot() = logger;
  }

  bool has_global_logger() {
    return registered_logger() != nullptr;
  }

  bool log(LogLevel level, std::string category, std::string message, LogSourceLocation location) {
    if (Logger *logger = registered_logger(); logger != nullptr) {
      return logger->log(level, std::move(category), std::move(message), location);
    }

    return false;
  }

  bool trace(std::string category, std::string message, LogSourceLocation location) {
    return log(LogLevel::trace, std::move(category), std::move(message), location);
  }

  bool debug(std::string category, std::string message, LogSourceLocation location) {
    return log(LogLevel::debug, std::move(category), std::move(message), location);
  }

  bool info(std::string category, std::string message, LogSourceLocation location) {
    return log(LogLevel::info, std::move(category), std::move(message), location);
  }

  bool warn(std::string category, std::string message, LogSourceLocation location) {
    return log(LogLevel::warning, std::move(category), std::move(message), location);
  }

  bool error(std::string category, std::string message, LogSourceLocation location) {
    return log(LogLevel::error, std::move(category), std::move(message), location);
  }

  void set_minimum_level(LogLevel minimumLevel) {
    if (Logger *logger = registered_logger(); logger != nullptr) {
      logger->set_minimum_level(minimumLevel);
    }
  }

  void set_file_sink(LogSink sink) {
    if (Logger *logger = registered_logger(); logger != nullptr) {
      logger->set_file_sink(std::move(sink));
    }
  }

  void set_file_minimum_level(LogLevel minimumLevel) {
    if (Logger *logger = registered_logger(); logger != nullptr) {
      logger->set_file_minimum_level(minimumLevel);
    }
  }

  void set_debugger_console_minimum_level(LogLevel minimumLevel) {
    if (Logger *logger = registered_logger(); logger != nullptr) {
      logger->set_debugger_console_minimum_level(minimumLevel);
    }
  }

  void set_startup_debug_enabled(bool enabled) {
    if (Logger *logger = registered_logger(); logger != nullptr) {
      logger->set_startup_debug_enabled(enabled);
    }
  }

  std::vector<LogEntry> snapshot(LogLevel minimumLevel) {
    if (const Logger *logger = registered_logger(); logger != nullptr) {
      return logger->snapshot(minimumLevel);
    }

    return {};
  }

  std::string format_startup_console_line(LogLevel level, std::string_view category, std::string_view message) {
    std::string line = std::string("[") + to_string(startup_console_level(level)) + "] ";
    if (!category.empty()) {
      line.append(category.data(), category.size());
      line.append(": ");
    }
    line.append(message.data(), message.size());
    return line;
  }

  void set_startup_console_enabled(bool enabled) {
    startup_console_enabled_slot() = enabled;
  }

  bool startup_console_enabled() {
    return startup_console_enabled_slot();
  }

  void print_startup_console_line(LogLevel level, std::string_view category, std::string_view message) {
    if (!startup_console_enabled()) {
      return;
    }

#if defined(NXDK)
    const std::string line = format_startup_console_line(level, category, message);
    debugPrint("%s\n", line.c_str());
#else
    (void) level;
    (void) category;
    (void) message;
#endif
  }

  Logger::Logger(std::size_t capacity, TimestampProvider timestampProvider):
      capacity_(capacity == 0 ? 1 : capacity),
      timestampProvider_(timestampProvider ? std::move(timestampProvider) : TimestampProvider(current_local_timestamp)) {}

  std::size_t Logger::capacity() const {
    return capacity_;
  }

  void Logger::set_minimum_level(LogLevel minimumLevel) {
    minimumLevel_ = minimumLevel;
  }

  LogLevel Logger::minimum_level() const {
    return minimumLevel_;
  }

  void Logger::set_file_sink(LogSink sink) {
    fileSink_ = std::move(sink);
  }

  void Logger::set_file_minimum_level(LogLevel minimumLevel) {
    fileMinimumLevel_ = minimumLevel;
  }

  LogLevel Logger::file_minimum_level() const {
    return fileMinimumLevel_;
  }

  void Logger::set_startup_debug_enabled(bool enabled) {
    startupDebugEnabled_ = enabled;
  }

  bool Logger::startup_debug_enabled() const {
    return startupDebugEnabled_;
  }

  void Logger::set_debugger_console_minimum_level(LogLevel minimumLevel) {
    debuggerConsoleMinimumLevel_ = minimumLevel;
  }

  LogLevel Logger::debugger_console_minimum_level() const {
    return debuggerConsoleMinimumLevel_;
  }

  bool Logger::should_log(LogLevel level) const {
    if (is_enabled(level, minimumLevel_)) {
      return true;
    }
    if (startupDebugEnabled_) {
      return true;
    }
    if (fileSink_ && is_enabled(level, fileMinimumLevel_)) {
      return true;
    }
    if (is_enabled(level, debuggerConsoleMinimumLevel_)) {
      return true;
    }

    return std::any_of(sinks_.begin(), sinks_.end(), [level](const RegisteredSink &registeredSink) {
      return registeredSink.sink && is_enabled(level, registeredSink.minimumLevel);
    });
  }

  bool Logger::log(LogLevel level, std::string category, std::string message, LogSourceLocation location) {
    if (!should_log(level)) {
      return false;
    }

    LogEntry entry {
      nextSequence_,
      level,
      std::move(category),
      std::move(message),
      timestampProvider_(),
      location,
    };
    ++nextSequence_;

    if (is_enabled(level, minimumLevel_)) {
      if (entries_.size() == capacity_) {
        entries_.pop_front();
      }

      entries_.push_back(entry);
    }

    if (startupDebugEnabled_) {
      print_startup_console_line(entry.level, entry.category, entry.message);
    }
    if (fileSink_ && is_enabled(level, fileMinimumLevel_)) {
      fileSink_(entry);
    }
    if (is_enabled(level, debuggerConsoleMinimumLevel_)) {
      emit_debugger_console_line(entry);
    }

    for (const RegisteredSink &registeredSink : sinks_) {
      if (registeredSink.sink && is_enabled(level, registeredSink.minimumLevel)) {
        registeredSink.sink(entry);
      }
    }

    return true;
  }

  bool Logger::trace(std::string category, std::string message, LogSourceLocation location) {
    return log(LogLevel::trace, std::move(category), std::move(message), location);
  }

  bool Logger::debug(std::string category, std::string message, LogSourceLocation location) {
    return log(LogLevel::debug, std::move(category), std::move(message), location);
  }

  bool Logger::info(std::string category, std::string message, LogSourceLocation location) {
    return log(LogLevel::info, std::move(category), std::move(message), location);
  }

  bool Logger::warn(std::string category, std::string message, LogSourceLocation location) {
    return log(LogLevel::warning, std::move(category), std::move(message), location);
  }

  bool Logger::error(std::string category, std::string message, LogSourceLocation location) {
    return log(LogLevel::error, std::move(category), std::move(message), location);
  }

  void Logger::add_sink(LogSink sink, LogLevel minimumLevel) {
    if (sink) {
      sinks_.push_back({minimumLevel, std::move(sink)});
    }
  }

  const std::deque<LogEntry> &Logger::entries() const {
    return entries_;
  }

  std::vector<LogEntry> Logger::snapshot(LogLevel minimumLevel) const {
    std::vector<LogEntry> filteredEntries;

    for (const LogEntry &entry : entries_) {
      if (is_enabled(entry.level, minimumLevel)) {
        filteredEntries.push_back(entry);
      }
    }

    return filteredEntries;
  }

}  // namespace logging
