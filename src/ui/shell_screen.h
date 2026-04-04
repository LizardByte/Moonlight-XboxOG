#pragma once

// nxdk includes
#include <hal/video.h>

// local includes
#include "src/app/client_state.h"
#include "src/logging/logger.h"

struct SDL_Window;

namespace ui {

  /**
   * @brief Run the interactive SDL shell after startup completes.
   *
   * @param window Shared SDL window created during startup.
   * @param videoMode Active output mode for the shell window.
   * @param state Mutable application state.
   * @param logger Structured logger used for diagnostics and overlay content.
   * @return 0 on normal exit, non-zero if initialization failed.
   */
  int run_shell(SDL_Window *window, const VIDEO_MODE &videoMode, app::ClientState &state, logging::Logger &logger);

}  // namespace ui
