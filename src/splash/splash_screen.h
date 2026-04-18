/**
 * @file src/splash/splash_screen.h
 * @brief Declares the splash screen workflow.
 */
#pragma once

// standard includes
#include <functional>

// nxdk includes
#include <hal/video.h>
#include <SDL.h>

namespace splash {

  /**
   * @brief Show the splash screen for a fixed duration.
   *
   * @param window SDL window used for splash rendering.
   * @param videoMode Active video mode used to size the splash layout.
   * @param durationMilliseconds Splash duration in milliseconds.
   */
  void show_splash_screen(SDL_Window *window, const VIDEO_MODE &videoMode, unsigned int durationMilliseconds = 1500U);

  /**
   * @brief Show the splash screen until the supplied callback reports completion.
   *
   * @param window SDL window used for splash rendering.
   * @param videoMode Active video mode used to size the splash layout.
   * @param keepShowing Callback that returns true while the splash screen should remain visible.
   */
  void show_splash_screen(SDL_Window *window, const VIDEO_MODE &videoMode, const std::function<bool()> &keepShowing);

}  // namespace splash
