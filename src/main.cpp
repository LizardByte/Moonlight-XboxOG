// nxdk includes
#include <SDL.h>
#include <hal/debug.h>
#include <windows.h>

// local includes
#include "src/app/client_state.h"
#include "src/logging/logger.h"
#include "src/network/runtime_network.h"
#include "src/splash/splash_screen.h"
#include "src/startup/host_storage.h"
#include "src/startup/memory_stats.h"
#include "src/startup/video_mode.h"
#include "src/ui/shell_screen.h"

namespace {

  int report_startup_failure(logging::Logger &logger, const char *category, const std::string &message) {
    logger.log(logging::LogLevel::error, category, message);
    debugPrint("%s\n", message.c_str());
    debugPrint("Holding failure screen for 5 seconds before exit.\n");
    Sleep(5000);
    return 1;
  }

}  // namespace

int main() {
  logging::Logger logger;
  logger.add_sink([](const logging::LogEntry &entry) {
    const std::string formattedEntry = logging::format_entry(entry);
    debugPrint("%s\n", formattedEntry.c_str());
  });

  app::ClientState clientState = app::create_initial_state();

  const startup::LoadSavedHostsResult loadedHosts = startup::load_saved_hosts();
  for (const std::string &warning : loadedHosts.warnings) {
    logger.log(logging::LogLevel::warning, "hosts", warning);
  }
  if (loadedHosts.fileFound) {
    app::replace_hosts(clientState, loadedHosts.hosts, "Loaded " + std::to_string(loadedHosts.hosts.size()) + " saved host(s)");
    logger.log(logging::LogLevel::info, "hosts", "Loaded " + std::to_string(loadedHosts.hosts.size()) + " saved host record(s)");
  }

  logger.log(logging::LogLevel::info, "app", std::string("Initial screen: ") + app::to_string(clientState.activeScreen));

  const startup::VideoModeSelection videoModeSelection = startup::select_best_video_mode();
  const VIDEO_MODE &bestVideoMode = videoModeSelection.bestVideoMode;

  XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);

  startup::log_video_modes(videoModeSelection);
  startup::log_memory_statistics();
  const network::RuntimeNetworkStatus runtimeNetworkStatus = network::initialize_runtime_networking();
  for (const std::string &line : network::format_runtime_network_status_lines(runtimeNetworkStatus)) {
    logger.log(runtimeNetworkStatus.ready ? logging::LogLevel::info : logging::LogLevel::warning, "network", line);
  }
  if (!runtimeNetworkStatus.ready) {
    clientState.statusMessage = runtimeNetworkStatus.summary;
  }

  Sleep(3000);

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

  logger.log(logging::LogLevel::info, "app", "Showing splash screen");
  splash::show_splash_screen(window, bestVideoMode, 2500U);

  logger.log(logging::LogLevel::info, "app", "Starting interactive shell");
  const int exitCode = ui::run_shell(window, bestVideoMode, clientState, logger);

  if (clientState.hostsDirty) {
    const startup::SaveSavedHostsResult saveResult = startup::save_saved_hosts(clientState.hosts);
    if (saveResult.success) {
      logger.log(logging::LogLevel::info, "hosts", "Saved host records before exit");
      clientState.hostsDirty = false;
    }
    else {
      logger.log(logging::LogLevel::error, "hosts", saveResult.errorMessage);
    }
  }

  SDL_DestroyWindow(window);
  SDL_Quit();
  return exitCode;
}
