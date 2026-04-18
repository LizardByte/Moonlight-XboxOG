/**
 * @file src/main.cpp
 * @brief Runs the Moonlight Xbox startup sequence and main loop.
 */
// nxdk includes
#include <hal/debug.h>
#include <SDL.h>
#include <windows.h>  // NOSONAR(cpp:S3806) nxdk requires lowercase header names

// standard includes
#include <array>
#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

// local includes
#include "src/app/client_state.h"
#include "src/app/settings_storage.h"
#include "src/logging/log_file.h"
#include "src/logging/logger.h"
#include "src/network/runtime_network.h"
#include "src/splash/splash_screen.h"
#include "src/startup/host_storage.h"
#include "src/startup/memory_stats.h"
#include "src/startup/video_mode.h"
#include "src/ui/shell_screen.h"

namespace {

  struct StartupTaskState {
    SDL_Thread *thread = nullptr;
    std::atomic<bool> completed = false;
    startup::LoadSavedHostsResult loadedHosts;
    network::RuntimeNetworkStatus runtimeNetworkStatus;
  };

  void apply_persisted_settings(app::ClientState &state, const app::AppSettings &settings) {
    state.settings.loggingLevel = settings.loggingLevel;
    state.settings.xemuConsoleLoggingLevel = settings.xemuConsoleLoggingLevel;
    state.settings.logViewerPlacement = settings.logViewerPlacement;
    state.settings.dirty = false;
  }

  void load_persisted_settings(app::ClientState &state) {
    const app::LoadAppSettingsResult loadResult = app::load_app_settings();
    apply_persisted_settings(state, loadResult.settings);

    for (const std::string &warning : loadResult.warnings) {
      logging::warn("settings", warning);
    }

    if (!loadResult.fileFound) {
      logging::info("settings", "No persisted settings file found. Using defaults.");
      return;
    }

    logging::info("settings", "Loaded persisted Moonlight settings");
    if (!loadResult.cleanupRequired) {
      return;
    }

    const app::SaveAppSettingsResult saveResult = app::save_app_settings(loadResult.settings);
    if (saveResult.success) {
      logging::info("settings", "Removed obsolete settings keys from the persisted configuration");
      return;
    }

    logging::warn("settings", saveResult.errorMessage.empty() ? "Failed to rewrite the settings file after cleaning obsolete keys" : saveResult.errorMessage);
  }

  int report_startup_failure(const char *category, const std::string &message) {
    logging::error(category, message);
    logging::print_startup_console_line(logging::LogLevel::warning, category, "Holding failure screen for 5 seconds before exit.");
    Sleep(5000);
    return 1;
  }

  void debug_print_startup_checkpoint(const char *message) {
    if (message == nullptr) {
      return;
    }

    logging::print_startup_console_line(logging::LogLevel::info, "startup", message);
  }

  void debug_print_video_mode_selection(const startup::VideoModeSelection &selection) {
    logging::print_startup_console_line(
      logging::LogLevel::info,
      "video",
      "Detected " + std::to_string(selection.availableVideoModes.size()) + " video mode(s)"
    );
    for (const std::string &line : startup::format_video_mode_lines(selection)) {
      logging::print_startup_console_line(logging::LogLevel::info, "video", line);
    }
  }

  void debug_print_encoder_settings(DWORD encoderSettings) {
    std::array<char, 160> messageBuffer {};
    std::snprintf(
      messageBuffer.data(),
      messageBuffer.size(),
      "Encoder settings: 0x%08lX (widescreen=%s, 480p=%s, 720p=%s, 1080i=%s)",
      encoderSettings,
      (encoderSettings & VIDEO_WIDESCREEN) != 0 ? "on" : "off",
      (encoderSettings & VIDEO_MODE_480P) != 0 ? "on" : "off",
      (encoderSettings & VIDEO_MODE_720P) != 0 ? "on" : "off",
      (encoderSettings & VIDEO_MODE_1080I) != 0 ? "on" : "off"
    );
    logging::print_startup_console_line(logging::LogLevel::info, "video", messageBuffer.data());
  }

  int run_startup_task(void *context) {
    auto *task = static_cast<StartupTaskState *>(context);
    if (task == nullptr) {
      return -1;
    }

    task->loadedHosts = startup::load_saved_hosts();
    task->runtimeNetworkStatus = network::initialize_runtime_networking();
    task->completed.store(true);
    return 0;
  }

  void finish_startup_task(app::ClientState &clientState, StartupTaskState *task) {
    if (task == nullptr) {
      return;
    }

    if (task->thread != nullptr) {
      int threadResult = 0;
      SDL_WaitThread(task->thread, &threadResult);
      (void) threadResult;
      task->thread = nullptr;
    }

    for (const std::string &warning : task->loadedHosts.warnings) {
      logging::warn("hosts", warning);
    }
    if (task->loadedHosts.fileFound) {
      app::replace_hosts(clientState, task->loadedHosts.hosts, "Loaded " + std::to_string(task->loadedHosts.hosts.size()) + " saved host(s)");
      logging::info("hosts", "Loaded " + std::to_string(task->loadedHosts.hosts.size()) + " saved host record(s)");
    }

    for (const std::string &line : network::format_runtime_network_status_lines(task->runtimeNetworkStatus)) {
      logging::log(task->runtimeNetworkStatus.ready ? logging::LogLevel::info : logging::LogLevel::warning, "network", line);
    }
    if (!task->runtimeNetworkStatus.ready) {
      clientState.shell.statusMessage = task->runtimeNetworkStatus.summary;
    }
  }

}  // namespace

/**
 * @brief Initialize the client runtime and enter the main shell loop.
 *
 * @return Process exit code.
 */
int main() {
  logging::Logger logger;
  logging::set_global_logger(&logger);
  logging::set_minimum_level(logging::LogLevel::trace);

  app::ClientState clientState = app::create_initial_state();
  load_persisted_settings(clientState);
  logging::set_file_minimum_level(clientState.settings.loggingLevel);
  logging::set_debugger_console_minimum_level(clientState.settings.xemuConsoleLoggingLevel);

  const std::string logFilePath = logging::default_log_file_path();
  logging::RuntimeLogFileSink runtimeLogFile(logFilePath);
  app::set_log_file_path(clientState, logFilePath);

  if (std::string logFileResetError; !runtimeLogFile.reset(&logFileResetError)) {
    logging::print_startup_console_line(
      logging::LogLevel::warning,
      "logging",
      logFileResetError.empty() ? "Failed to reset the runtime log file." : logFileResetError
    );
  }
  logging::set_file_sink([&runtimeLogFile](const logging::LogEntry &entry) {
    std::string ignoredError;
    runtimeLogFile.consume(entry, &ignoredError);
  });

  logging::info("app", std::string("Initial screen: ") + app::to_string(clientState.shell.activeScreen));
  debug_print_startup_checkpoint("Runtime logging initialized");
  debug_print_encoder_settings(XVideoGetEncoderSettings());

  const startup::VideoModeSelection videoModeSelection = startup::select_best_video_mode();
  const VIDEO_MODE &bestVideoMode = videoModeSelection.bestVideoMode;
  debug_print_video_mode_selection(videoModeSelection);
  startup::log_memory_statistics();

  debug_print_startup_checkpoint(
    (std::string("About to call XVideoSetMode with ") + std::to_string(bestVideoMode.width) + "x" + std::to_string(bestVideoMode.height) + ", bpp=" + std::to_string(bestVideoMode.bpp) + ", refresh=" + std::to_string(bestVideoMode.refresh)).c_str()
  );

  const BOOL setVideoModeResult = XVideoSetMode(bestVideoMode.width, bestVideoMode.height, bestVideoMode.bpp, bestVideoMode.refresh);
  debug_print_startup_checkpoint(setVideoModeResult ? "Returned from XVideoSetMode successfully" : "XVideoSetMode returned failure");

  debug_print_startup_checkpoint("About to call SDL_Init");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
    return report_startup_failure("sdl", std::string("SDL_Init failed: ") + SDL_GetError());
  }
  debug_print_startup_checkpoint("SDL_Init succeeded");

  debug_print_startup_checkpoint("About to create SDL window");
  SDL_Window *window = SDL_CreateWindow(
    "Moonlight Xbox",
    SDL_WINDOWPOS_UNDEFINED,
    SDL_WINDOWPOS_UNDEFINED,
    bestVideoMode.width,
    bestVideoMode.height,
    SDL_WINDOW_SHOWN
  );
  if (window == nullptr) {
    const int exitCode = report_startup_failure("sdl", std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    SDL_Quit();
    return exitCode;
  }
  debug_print_startup_checkpoint("SDL window creation succeeded");

  StartupTaskState startupTask {};
  debug_print_startup_checkpoint("Starting background startup task");
  startupTask.thread = SDL_CreateThread(run_startup_task, "startup-init", &startupTask);
  if (startupTask.thread == nullptr) {
    debug_print_startup_checkpoint("SDL_CreateThread failed; running startup task synchronously");
    run_startup_task(&startupTask);
  } else {
    debug_print_startup_checkpoint("Background startup task created");
  }

  logging::info("app", "Showing splash screen");
  debug_print_startup_checkpoint("About to show splash screen");
  logging::set_startup_debug_enabled(false);
  logging::set_startup_console_enabled(false);
  splash::show_splash_screen(window, bestVideoMode, [&startupTask]() {
    return !startupTask.completed.load();
  });

  finish_startup_task(clientState, &startupTask);
  startup::log_video_modes(videoModeSelection);

  logging::info("app", "Starting interactive shell");
  const int exitCode = ui::run_shell(window, bestVideoMode, clientState);

  if (clientState.hosts.dirty) {
    const startup::SaveSavedHostsResult saveResult = startup::save_saved_hosts(clientState.hosts.items);
    if (saveResult.success) {
      logging::info("hosts", "Saved host records before exit");
      clientState.hosts.dirty = false;
    } else {
      logging::error("hosts", saveResult.errorMessage);
    }
  }

  SDL_DestroyWindow(window);
  SDL_Quit();
  return exitCode;
}
