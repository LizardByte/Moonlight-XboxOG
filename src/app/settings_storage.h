/**
 * @file src/app/settings_storage.h
 * @brief Declares application settings persistence.
 */
#pragma once

// standard includes
#include <string>
#include <vector>

// local includes
#include "src/app/client_state.h"

namespace app {

  /**
   * @brief Persisted Moonlight user settings stored in TOML.
   */
  struct AppSettings {
    logging::LogLevel loggingLevel = logging::LogLevel::none;  ///< Minimum runtime log level written to the log file.
    logging::LogLevel xemuConsoleLoggingLevel = logging::LogLevel::none;  ///< Minimum runtime log level written through DbgPrint() for xemu's serial console.
    app::LogViewerPlacement logViewerPlacement = app::LogViewerPlacement::full;  ///< Preferred placement for the in-app log viewer.
    VIDEO_MODE preferredVideoMode {};  ///< Preferred stream resolution requested from the host.
    bool preferredVideoModeSet = false;  ///< True when preferredVideoMode contains a saved user preference.
    int streamFramerate = 20;  ///< Preferred stream frame rate in frames per second.
    int streamBitrateKbps = 1500;  ///< Preferred stream bitrate in kilobits per second.
    bool playAudioOnPc = false;  ///< True when the host PC should continue local audio playback during streaming.
    bool showPerformanceStats = false;  ///< True when the streaming overlay should remain visible over decoded video.
  };

  /**
   * @brief Result of loading the persisted Moonlight settings file.
   */
  struct LoadAppSettingsResult {
    AppSettings settings;  ///< Loaded settings, or defaults when the file is missing or invalid.
    std::vector<std::string> warnings;  ///< Non-fatal warnings encountered while loading the file.
    bool fileFound = false;  ///< True when the settings file existed on disk.
    bool cleanupRequired = false;  ///< True when legacy or unknown keys should be removed by rewriting the file.
  };

  /**
   * @brief Result of saving the persisted Moonlight settings file.
   */
  struct SaveAppSettingsResult {
    bool success = false;  ///< True when the settings file was written successfully.
    std::string errorMessage;  ///< Error detail when writing failed.
  };

  /**
   * @brief Return the default path used for the persisted Moonlight settings file.
   *
   * @return Default TOML settings file path.
   */
  std::string default_settings_path();

  /**
   * @brief Load the persisted Moonlight settings file.
   *
   * @param filePath Settings file to read.
   * @return Loaded settings plus any non-fatal warnings.
   */
  LoadAppSettingsResult load_app_settings(const std::string &filePath = default_settings_path());

  /**
   * @brief Save the persisted Moonlight settings file.
   *
   * @param settings Settings snapshot to write.
   * @param filePath Settings file to write.
   * @return Save result including success state and error detail.
   */
  SaveAppSettingsResult save_app_settings(const AppSettings &settings, const std::string &filePath = default_settings_path());

}  // namespace app
