#pragma once

// nxdk includes
#include <hal/video.h>

// local includes
#include "src/app/client_state.h"

struct SDL_Window;

namespace ui {

  /**
   * @brief Run the interactive SDL shell after startup completes.
   *
   * @param window Shared SDL window created during startup.
   * @param videoMode Active output mode for the shell window.
   * @param state Mutable application state.
   * @return 0 on normal exit, non-zero if initialization failed.
   */
  int run_shell(
    SDL_Window *window,
    const VIDEO_MODE &videoMode,
    app::ClientState &state
  );

}  // namespace ui
