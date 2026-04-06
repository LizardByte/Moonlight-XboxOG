// class header include
#include "src/splash/splash_layout.h"

// standard includes
#include <algorithm>
#include <cmath>

namespace {

  constexpr float SPLASH_LOGO_MAX_WIDTH_RATIO = 0.72f;
  constexpr float SPLASH_LOGO_MAX_HEIGHT_RATIO = 0.32f;
  constexpr float SPLASH_ASPECT_RATIO_EPSILON = 0.05f;

  int clamp_scaled_dimension(float value) {
    if (const auto scaledValue = static_cast<int>(value); scaledValue > 0) {
      return scaledValue;
    }

    return 1;
  }

}  // namespace

namespace splash {

  float get_framebuffer_aspect_ratio(const VIDEO_MODE &videoMode) {
    return static_cast<float>(videoMode.width) / static_cast<float>(videoMode.height);
  }

  float get_display_aspect_ratio(const VIDEO_MODE &videoMode, unsigned long encoderSettings) {
    const float framebufferAspectRatio = get_framebuffer_aspect_ratio(videoMode);
    const float preferredDisplayAspectRatio = ((encoderSettings & VIDEO_WIDESCREEN) != 0UL) ? (16.0f / 9.0f) : (4.0f / 3.0f);

    if (std::fabs(framebufferAspectRatio - preferredDisplayAspectRatio) > SPLASH_ASPECT_RATIO_EPSILON) {
      return preferredDisplayAspectRatio;
    }

    return framebufferAspectRatio;
  }

  int calculate_display_width(int screenHeight, const VIDEO_MODE &videoMode, unsigned long encoderSettings) {
    return clamp_scaled_dimension(static_cast<float>(screenHeight) * get_display_aspect_ratio(videoMode, encoderSettings));
  }

  float get_logo_width_aspect_correction(const VIDEO_MODE &videoMode, unsigned long encoderSettings) {
    return get_framebuffer_aspect_ratio(videoMode) / get_display_aspect_ratio(videoMode, encoderSettings);
  }

  SplashLayout calculate_logo_destination(
    int screenWidth,
    int screenHeight,
    int logoWidth,
    int logoHeight,
    const VIDEO_MODE &videoMode,
    unsigned long encoderSettings
  ) {
    const float correctedLogoWidth = static_cast<float>(logoWidth) * get_logo_width_aspect_correction(videoMode, encoderSettings);
    const float maxLogoWidth = static_cast<float>(screenWidth) * SPLASH_LOGO_MAX_WIDTH_RATIO;
    const float maxLogoHeight = static_cast<float>(screenHeight) * SPLASH_LOGO_MAX_HEIGHT_RATIO;
    const float widthScale = maxLogoWidth / correctedLogoWidth;
    const float heightScale = maxLogoHeight / static_cast<float>(logoHeight);
    const float scale = std::min(widthScale, heightScale);

    SplashLayout layout {};
    layout.width = clamp_scaled_dimension(correctedLogoWidth * scale);
    layout.height = clamp_scaled_dimension(static_cast<float>(logoHeight) * scale);
    layout.x = (screenWidth - layout.width) / 2;
    layout.y = (screenHeight - layout.height) / 2;
    return layout;
  }

}  // namespace splash
