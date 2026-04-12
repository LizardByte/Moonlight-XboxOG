// class header include
#include "src/logging/global_logger.h"

// standard includes
#include <utility>

namespace {

  logging::Logger *g_globalLogger = nullptr;

}  // namespace

namespace logging {

  const GlobalLoggerProxy logger {};

  void set_global_logger(Logger *logger) {
    g_globalLogger = logger;
  }

  Logger *global_logger() {
    return g_globalLogger;
  }

  bool has_global_logger() {
    return global_logger() != nullptr;
  }

  bool GlobalLoggerProxy::available() const {
    return has_global_logger();
  }

  Logger *GlobalLoggerProxy::get() const {
    return global_logger();
  }

  bool GlobalLoggerProxy::log(LogLevel level, std::string category, std::string message, LogSourceLocation location) const {
    if (Logger *registeredLogger = global_logger(); registeredLogger != nullptr) {
      return registeredLogger->log(level, std::move(category), std::move(message), location);
    }

    return false;
  }

  bool GlobalLoggerProxy::trace(std::string category, std::string message, LogSourceLocation location) const {
    return log(LogLevel::trace, std::move(category), std::move(message), location);
  }

  bool GlobalLoggerProxy::debug(std::string category, std::string message, LogSourceLocation location) const {
    return log(LogLevel::debug, std::move(category), std::move(message), location);
  }

  bool GlobalLoggerProxy::info(std::string category, std::string message, LogSourceLocation location) const {
    return log(LogLevel::info, std::move(category), std::move(message), location);
  }

  bool GlobalLoggerProxy::warn(std::string category, std::string message, LogSourceLocation location) const {
    return log(LogLevel::warning, std::move(category), std::move(message), location);
  }

  bool GlobalLoggerProxy::error(std::string category, std::string message, LogSourceLocation location) const {
    return log(LogLevel::error, std::move(category), std::move(message), location);
  }

}  // namespace logging
