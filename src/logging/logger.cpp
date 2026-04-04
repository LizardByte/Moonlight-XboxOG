// class header include
#include "src/logging/logger.h"

// standard includes
#include <utility>

namespace {

  bool is_enabled(logging::LogLevel candidateLevel, logging::LogLevel minimumLevel) {
    return static_cast<int>(candidateLevel) >= static_cast<int>(minimumLevel);
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

  std::string format_entry(const LogEntry &entry) {
    if (entry.category.empty()) {
      return std::string("[") + to_string(entry.level) + "] " + entry.message;
    }

    return std::string("[") + to_string(entry.level) + "] " + entry.category + ": " + entry.message;
  }

  Logger::Logger(std::size_t capacity)
    : capacity_(capacity == 0 ? 1 : capacity),
      minimumLevel_(LogLevel::info),
      nextSequence_(1) {}

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
