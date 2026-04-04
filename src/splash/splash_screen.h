#pragma once

// nxdk includes
#include <hal/video.h>

struct SDL_Window;

namespace splash {

  void show_splash_screen(SDL_Window *window, const VIDEO_MODE &videoMode, unsigned int durationMilliseconds = 1500U);

}
