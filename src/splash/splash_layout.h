#pragma once

#include "src/nxdk/hal/video.h"

namespace splash {

  /**
   * @brief Rectangle used for splash image placement.
   */
  struct SplashLayout {
    int x;
    int y;
    int width;
    int height;
  };

  /**
   * @brief Return the framebuffer aspect ratio for a video mode.
   *
   * @param videoMode The video mode being rendered.
   * @return The framebuffer width divided by height.
   */
  float get_framebuffer_aspect_ratio(const VIDEO_MODE &videoMode);

  /**
   * @brief Return the display aspect ratio after encoder corrections.
   *
   * Standard-definition framebuffers may need to be displayed with a corrected
   * aspect ratio based on the active encoder settings.
   *
   * @param videoMode The video mode being rendered.
   * @param encoderSettings The value returned by XVideoGetEncoderSettings().
   * @return The effective display aspect ratio.
   */
  float get_display_aspect_ratio(const VIDEO_MODE &videoMode, unsigned long encoderSettings);

  /**
   * @brief Return the width correction factor applied before scaling the logo.
   *
   * @param videoMode The video mode being rendered.
   * @param encoderSettings The value returned by XVideoGetEncoderSettings().
   * @return The correction multiplier applied to the source logo width.
   */
  float get_logo_width_aspect_correction(const VIDEO_MODE &videoMode, unsigned long encoderSettings);

  /**
   * @brief Calculate the centered destination rectangle for the splash logo.
   *
   * @param screenWidth Width of the destination surface.
   * @param screenHeight Height of the destination surface.
   * @param logoWidth Width of the source logo.
   * @param logoHeight Height of the source logo.
   * @param videoMode Active video mode.
   * @param encoderSettings The value returned by XVideoGetEncoderSettings().
   * @return The scaled and centered destination rectangle.
   */
  SplashLayout calculate_logo_destination(
    int screenWidth,
    int screenHeight,
    int logoWidth,
    int logoHeight,
    const VIDEO_MODE &videoMode,
    unsigned long encoderSettings
  );

}  // namespace splash
