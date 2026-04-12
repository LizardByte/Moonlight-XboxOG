// class header include
#include "src/app/settings_storage.h"

// standard includes
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32) && !defined(__MINGW32__) && !defined(__MINGW64__)
// needed for toml++
extern "C" FILE *_wfopen(const wchar_t *path, const wchar_t *mode);
#endif

#define TOML_EXCEPTIONS 0
#define TOML_ENABLE_WINDOWS_COMPAT 0
#include <toml++/toml.hpp>

// local includes
#include "src/platform/error_utils.h"
#include "src/platform/filesystem_utils.h"
#include "src/startup/storage_paths.h"

namespace {

  using namespace std::string_view_literals;
  using platform::append_error;

  constexpr std::string_view SETTINGS_FILE_NAME = "moonlight.toml";

  std::string read_all_text(FILE *file) {
    std::string content;
    std::vector<char> buffer(4096);

    while (true) {
      const std::size_t bytesRead = std::fread(buffer.data(), 1, buffer.size(), file);
      if (bytesRead > 0U) {
        content.append(buffer.data(), bytesRead);
      }
      if (bytesRead < buffer.size()) {
        break;
      }
    }

    return content;
  }

  bool write_text_file(const std::string &filePath, std::string_view content, std::string *errorMessage) {
    if (!platform::ensure_directory_exists(platform::parent_directory(filePath), errorMessage)) {
      return false;
    }

    FILE *file = std::fopen(filePath.c_str(), "wb");
    if (file == nullptr) {
      return append_error(errorMessage, "Failed to open settings file '" + filePath + "' for writing: " + std::strerror(errno));
    }

    if (const std::size_t bytesWritten = std::fwrite(content.data(), 1, content.size(), file); bytesWritten != content.size()) {
      const std::string writeError = std::strerror(errno);
      std::fclose(file);
      return append_error(errorMessage, "Failed to write settings file '" + filePath + "': " + writeError);
    }

    if (std::fclose(file) != 0) {
      return append_error(errorMessage, "Failed to finalize settings file '" + filePath + "': " + std::strerror(errno));
    }

    return true;
  }

  std::string ascii_lowercase(std::string_view text) {
    std::string normalized;
    normalized.reserve(text.size());
    for (const unsigned char character : text) {
      normalized.push_back(static_cast<char>(std::tolower(character)));
    }
    return normalized;
  }

  const char *logging_level_text(logging::LogLevel level) {
    switch (level) {
      case logging::LogLevel::trace:
        return "trace";
      case logging::LogLevel::debug:
        return "debug";
      case logging::LogLevel::info:
        return "info";
      case logging::LogLevel::warning:
        return "warning";
      case logging::LogLevel::error:
        return "error";
      case logging::LogLevel::none:
        return "none";
    }

    return "none";
  }

  const char *log_viewer_placement_text(app::LogViewerPlacement placement) {
    switch (placement) {
      case app::LogViewerPlacement::full:
        return "full";
      case app::LogViewerPlacement::left:
        return "left";
      case app::LogViewerPlacement::right:
        return "right";
    }

    return "full";
  }

  bool try_parse_logging_level(std::string_view text, logging::LogLevel *level) {
    const std::string normalized = ascii_lowercase(text);
    if (normalized == "trace") {
      if (level != nullptr) {
        *level = logging::LogLevel::trace;
      }
      return true;
    }
    if (normalized == "debug") {
      if (level != nullptr) {
        *level = logging::LogLevel::debug;
      }
      return true;
    }
    if (normalized == "info") {
      if (level != nullptr) {
        *level = logging::LogLevel::info;
      }
      return true;
    }
    if (normalized == "warning" || normalized == "warn") {
      if (level != nullptr) {
        *level = logging::LogLevel::warning;
      }
      return true;
    }
    if (normalized == "error") {
      if (level != nullptr) {
        *level = logging::LogLevel::error;
      }
      return true;
    }
    if (normalized == "none") {
      if (level != nullptr) {
        *level = logging::LogLevel::none;
      }
      return true;
    }

    return false;
  }

  bool try_parse_log_viewer_placement(std::string_view text, app::LogViewerPlacement *placement) {
    const std::string normalized = ascii_lowercase(text);
    if (normalized == "full") {
      if (placement != nullptr) {
        *placement = app::LogViewerPlacement::full;
      }
      return true;
    }
    if (normalized == "left") {
      if (placement != nullptr) {
        *placement = app::LogViewerPlacement::left;
      }
      return true;
    }
    if (normalized == "right") {
      if (placement != nullptr) {
        *placement = app::LogViewerPlacement::right;
      }
      return true;
    }

    return false;
  }

  void append_invalid_value_warning(std::vector<std::string> *warnings, const std::string &filePath, std::string_view keyPath, std::string_view valueText) {
    if (warnings == nullptr) {
      return;
    }

    warnings->push_back(
      "Ignoring invalid value '" + std::string(valueText) + "' for settings key '" + std::string(keyPath) + "' in '" + filePath + "'"
    );
  }

  void append_cleanup_warning(std::vector<std::string> *warnings, const std::string &filePath, std::string_view keyPath, std::string_view reason) {
    if (warnings == nullptr) {
      return;
    }

    warnings->push_back(
      "Will remove " + std::string(reason) + " settings key '" + std::string(keyPath) + "' from '" + filePath + "' on the next save"
    );
  }

  void mark_cleanup_required(app::LoadAppSettingsResult *result, const std::string &filePath, std::string_view keyPath, std::string_view reason) {
    if (result == nullptr) {
      return;
    }

    result->cleanupRequired = true;
    append_cleanup_warning(&result->warnings, filePath, keyPath, reason);
  }

  std::string format_settings_toml(const app::AppSettings &settings) {
    std::string content;
    content += "# Moonlight Xbox OG user settings\n";
    content += "# This file is safe to edit by hand.\n\n";
    content += "[logging]\n";
    content += "# Controls runtime file logging. Use none to avoid disk writes on slow Xbox drives.\n";
    content += std::string("file_minimum_level = \"") + logging_level_text(settings.loggingLevel) + "\"\n";
    content += "# Controls runtime DbgPrint() output for xemu's serial console.\n";
    content += "# Start xemu with -device lpc47m157 -serial stdio to see this output.\n";
    content += std::string("xemu_console_minimum_level = \"") + logging_level_text(settings.xemuConsoleLoggingLevel) + "\"\n\n";
    content += "[ui]\n";
    content += "# Preferred placement for the in-app log viewer.\n";
    content += std::string("log_viewer_placement = \"") + log_viewer_placement_text(settings.logViewerPlacement) + "\"\n";
    return content;
  }

  void inspect_logging_keys(const toml::table &loggingTable, const std::string &filePath, app::LoadAppSettingsResult *result) {
    for (const auto &[rawKey, node] : loggingTable) {
      const std::string key(rawKey.str());
      if (key == "file_minimum_level" || key == "xemu_console_minimum_level") {
        continue;
      }
      if (key == "minimum_level") {
        (void) node;
        mark_cleanup_required(result, filePath, "logging.minimum_level", "legacy");
        continue;
      }

      (void) node;
      mark_cleanup_required(result, filePath, std::string("logging.") + key, "obsolete");
    }
  }

  void inspect_ui_keys(const toml::table &uiTable, const std::string &filePath, app::LoadAppSettingsResult *result) {
    for (const auto &[rawKey, node] : uiTable) {
      const std::string key(rawKey.str());
      if (key == "log_viewer_placement") {
        continue;
      }

      (void) node;
      mark_cleanup_required(result, filePath, std::string("ui.") + key, "obsolete");
    }
  }

  void inspect_top_level_keys(const toml::table &settingsTable, const std::string &filePath, app::LoadAppSettingsResult *result) {
    for (const auto &[rawKey, node] : settingsTable) {
      const std::string key(rawKey.str());
      if (key == "logging") {
        if (const auto *loggingTable = node.as_table(); loggingTable != nullptr) {
          inspect_logging_keys(*loggingTable, filePath, result);
        }
        continue;
      }
      if (key == "ui") {
        if (const auto *uiTable = node.as_table(); uiTable != nullptr) {
          inspect_ui_keys(*uiTable, filePath, result);
        }
        continue;
      }
      if (key == "debug") {
        mark_cleanup_required(result, filePath, "debug", "obsolete");
        continue;
      }

      mark_cleanup_required(result, filePath, key, "obsolete");
    }
  }

}  // namespace

namespace app {

  std::string default_settings_path() {
    return startup::default_storage_path(SETTINGS_FILE_NAME);
  }

  LoadAppSettingsResult load_app_settings(const std::string &filePath) {
    LoadAppSettingsResult result {};

    FILE *file = std::fopen(filePath.c_str(), "rb");
    if (file == nullptr) {
      if (errno != ENOENT) {
        result.warnings.push_back("Failed to open settings file '" + filePath + "': " + std::strerror(errno));
      }
      return result;
    }

    result.fileFound = true;
    const std::string fileContent = read_all_text(file);
    if (std::ferror(file) != 0) {
      result.warnings.push_back("Failed while reading settings file '" + filePath + "': " + std::strerror(errno));
      std::fclose(file);
      return result;
    }
    std::fclose(file);

    const toml::parse_result parsed = toml::parse(std::string_view {fileContent}, std::string_view {filePath});
    if (!parsed) {
      result.warnings.push_back("Failed to parse settings file '" + filePath + "': " + std::string(parsed.error().description()));
      return result;
    }

    const toml::table &settingsTable = parsed.table();
    inspect_top_level_keys(settingsTable, filePath, &result);

    const auto loggingLevelNode = settingsTable["logging"]["file_minimum_level"];
    if (loggingLevelNode) {
      if (const auto loggingLevelText = loggingLevelNode.value<std::string>(); loggingLevelText) {
        if (!try_parse_logging_level(*loggingLevelText, &result.settings.loggingLevel)) {
          append_invalid_value_warning(&result.warnings, filePath, "logging.file_minimum_level", *loggingLevelText);
        }
      } else {
        append_invalid_value_warning(&result.warnings, filePath, "logging.file_minimum_level", "<non-string>");
      }
    }

    const auto legacyLoggingLevelNode = settingsTable["logging"]["minimum_level"];
    if (legacyLoggingLevelNode && !loggingLevelNode) {
      if (const auto loggingLevelText = legacyLoggingLevelNode.value<std::string>(); loggingLevelText) {
        if (!try_parse_logging_level(*loggingLevelText, &result.settings.loggingLevel)) {
          append_invalid_value_warning(&result.warnings, filePath, "logging.minimum_level", *loggingLevelText);
        }
      } else {
        append_invalid_value_warning(&result.warnings, filePath, "logging.minimum_level", "<non-string>");
      }
    }

    const auto xemuConsoleLoggingLevelNode = settingsTable["logging"]["xemu_console_minimum_level"];
    if (xemuConsoleLoggingLevelNode) {
      if (const auto xemuConsoleLoggingLevelText = xemuConsoleLoggingLevelNode.value<std::string>(); xemuConsoleLoggingLevelText) {
        if (!try_parse_logging_level(*xemuConsoleLoggingLevelText, &result.settings.xemuConsoleLoggingLevel)) {
          append_invalid_value_warning(&result.warnings, filePath, "logging.xemu_console_minimum_level", *xemuConsoleLoggingLevelText);
        }
      } else {
        append_invalid_value_warning(&result.warnings, filePath, "logging.xemu_console_minimum_level", "<non-string>");
      }
    }

    const auto logViewerPlacementNode = settingsTable["ui"]["log_viewer_placement"];
    if (logViewerPlacementNode) {
      if (const auto logViewerPlacementText = logViewerPlacementNode.value<std::string>(); logViewerPlacementText) {
        if (!try_parse_log_viewer_placement(*logViewerPlacementText, &result.settings.logViewerPlacement)) {
          append_invalid_value_warning(&result.warnings, filePath, "ui.log_viewer_placement", *logViewerPlacementText);
        }
      } else {
        append_invalid_value_warning(&result.warnings, filePath, "ui.log_viewer_placement", "<non-string>");
      }
    }

    return result;
  }

  SaveAppSettingsResult save_app_settings(const AppSettings &settings, const std::string &filePath) {
    std::string errorMessage;
    if (!write_text_file(filePath, format_settings_toml(settings), &errorMessage)) {
      return {false, errorMessage};
    }

    return {true, {}};
  }

}  // namespace app
