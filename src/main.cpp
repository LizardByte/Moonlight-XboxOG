// nxdk includes
#include <hal/debug.h>
#include <SDL.h>
#include <windows.h>

// standard includes
#include <atomic>
#include <vector>

// local includes
#include "src/app/client_state.h"
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

  int report_startup_failure(logging::Logger &logger, const char *category, const std::string &message) {
    logger.log(logging::LogLevel::error, category, message);
    debugPrint("%s\n", message.c_str());
    debugPrint("Holding failure screen for 5 seconds before exit.\n");
    Sleep(5000);
    return 1;
  }

  int run_startup_task(void *context) {
    StartupTaskState *task = static_cast<StartupTaskState *>(context);
    if (task == nullptr) {
      return -1;
    }

    task->loadedHosts = startup::load_saved_hosts();
    task->runtimeNetworkStatus = network::initialize_runtime_networking();
    task->completed.store(true, std::memory_order_release);
    return 0;
  }

  void finish_startup_task(logging::Logger &logger, app::ClientState &clientState, StartupTaskState *task) {
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
      logger.log(logging::LogLevel::warning, "hosts", warning);
    }
    if (task->loadedHosts.fileFound) {
      app::replace_hosts(clientState, task->loadedHosts.hosts, "Loaded " + std::to_string(task->loadedHosts.hosts.size()) + " saved host(s)");
      logger.log(logging::LogLevel::info, "hosts", "Loaded " + std::to_string(task->loadedHosts.hosts.size()) + " saved host record(s)");
    }

    for (const std::string &line : network::format_runtime_network_status_lines(task->runtimeNetworkStatus)) {
      logger.log(task->runtimeNetworkStatus.ready ? logging::LogLevel::info : logging::LogLevel::warning, "network", line);
    }
    if (!task->runtimeNetworkStatus.ready) {
      clientState.statusMessage = task->runtimeNetworkStatus.summary;
    }
  }

}  // namespace

int main() {
  logging::Logger logger;
  app::ClientState clientState = app::create_initial_state();
  const std::string logFilePath = logging::default_log_file_path();
  app::set_log_file_path(clientState, logFilePath);
  std::string logResetError;
  if (!logging::reset_log_file(logFilePath, &logResetError)) {
    debugPrint("Failed to reset runtime log file %s: %s\n", logFilePath.c_str(), logResetError.c_str());
  }
  logger.add_sink([logFilePath](const logging::LogEntry &entry) {
    std::string ignoredError;
    logging::append_log_file_entry(entry, logFilePath, &ignoredError);
  });
  logger.set_minimum_level(clientState.loggingLevel);
  logger.log(logging::LogLevel::info, "app", std::string("Initial screen: ") + app::to_string(clientState.activeScreen));
  logger.log(logging::LogLevel::info, "logging", "Writing runtime logs to " + logFilePath);

  const startup::VideoModeSelection videoModeSelection = startup::select_best_video_mode();
  const VIDEO_MODE &bestVideoMode = videoModeSelection.bestVideoMode;

  XVideoSetMode(bestVideoMode.width, bestVideoMode.height, bestVideoMode.bpp, bestVideoMode.refresh);

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
    return report_startup_failure(logger, "sdl", std::string("SDL_Init failed: ") + SDL_GetError());
  }

  SDL_Window *window = SDL_CreateWindow(
    "Moonlight Xbox",
    SDL_WINDOWPOS_UNDEFINED,
    SDL_WINDOWPOS_UNDEFINED,
    bestVideoMode.width,
    bestVideoMode.height,
    SDL_WINDOW_SHOWN
  );
  if (window == nullptr) {
    const int exitCode = report_startup_failure(logger, "sdl", std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    SDL_Quit();
    return exitCode;
  }

  StartupTaskState startupTask {};
  startupTask.thread = SDL_CreateThread(run_startup_task, "startup-init", &startupTask);
  if (startupTask.thread == nullptr) {
    run_startup_task(&startupTask);
  }

  logger.log(logging::LogLevel::info, "app", "Showing splash screen");
  splash::show_splash_screen(window, bestVideoMode, [&startupTask]() {
    return !startupTask.completed.load(std::memory_order_acquire);
  });

  finish_startup_task(logger, clientState, &startupTask);
  for (const std::string &line : startup::format_video_mode_lines(videoModeSelection)) {
    logger.log(logging::LogLevel::info, "video", line);
  }
  for (const std::string &line : startup::format_memory_statistics_lines()) {
    logger.log(logging::LogLevel::info, "memory", line);
  }

  logger.log(logging::LogLevel::info, "app", "Starting interactive shell");
  const int exitCode = ui::run_shell(window, bestVideoMode, clientState, logger);

  if (clientState.hostsDirty) {
    const startup::SaveSavedHostsResult saveResult = startup::save_saved_hosts(clientState.hosts);
    if (saveResult.success) {
      logger.log(logging::LogLevel::info, "hosts", "Saved host records before exit");
      clientState.hostsDirty = false;
    } else {
      logger.log(logging::LogLevel::error, "hosts", saveResult.errorMessage);
    }
  }

  SDL_DestroyWindow(window);
  SDL_Quit();
  return exitCode;
}
