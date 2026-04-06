// class header include
#include "src/logging/logger.h"

// standard includes
#include <chrono>
#include <cstdio>
#include <ctime>
#include <utility>

#if defined(_WIN32)
  #include <windows.h>
#endif

namespace {

  bool is_enabled(logging::LogLevel candidateLevel, logging::LogLevel minimumLevel) {
    return static_cast<int>(candidateLevel) >= static_cast<int>(minimumLevel);
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
    }

    return "UNKNOWN";
  }

  std::string format_timestamp(const LogTimestamp &timestamp) {
    char buffer[32] = {};
    const bool validTimestamp = is_valid_timestamp(timestamp);
    std::snprintf(
      buffer,
      sizeof(buffer),
      "%04d-%02d-%02d %02d:%02d:%02d.%03d",
      validTimestamp ? timestamp.year : 0,
      validTimestamp ? timestamp.month : 0,
      validTimestamp ? timestamp.day : 0,
      validTimestamp ? timestamp.hour : 0,
      validTimestamp ? timestamp.minute : 0,
      validTimestamp ? timestamp.second : 0,
      validTimestamp ? timestamp.millisecond : 0
    );
    return buffer;
  }

  std::string format_entry(const LogEntry &entry) {
    if (entry.category.empty()) {
      return std::string("[") + to_string(entry.level) + "] " + entry.message;
    }

    return std::string("[") + to_string(entry.level) + "] " + entry.category + ": " + entry.message;
  }

  Logger::Logger(std::size_t capacity, TimestampProvider timestampProvider):
      capacity_(capacity == 0 ? 1 : capacity),
      minimumLevel_(LogLevel::info),
      nextSequence_(1),
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

  bool Logger::should_log(LogLevel level) const {
    return is_enabled(level, minimumLevel_);
  }

  bool Logger::log(LogLevel level, std::string category, std::string message) {
    if (!should_log(level)) {
      return false;
    }

    LogEntry entry {
      nextSequence_,
      level,
      std::move(category),
      std::move(message),
      timestampProvider_(),
    };
    ++nextSequence_;

    if (entries_.size() == capacity_) {
      entries_.pop_front();
    }

    entries_.push_back(entry);

    for (const LogSink &sink : sinks_) {
      if (sink) {
        sink(entries_.back());
      }
    }

    return true;
  }

  void Logger::add_sink(LogSink sink) {
    if (sink) {
      sinks_.push_back(std::move(sink));
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
