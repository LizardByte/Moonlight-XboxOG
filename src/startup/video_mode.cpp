/**
 * @file src/startup/video_mode.cpp
 * @brief Implements video mode selection helpers.
 */
// class header include
#include "src/startup/video_mode.h"

// local includes
#include "src/logging/logger.h"

// standard includes
#include <array>

namespace startup {

  namespace {

    struct StreamResolutionPreset {
      int width;
      int height;
    };

    constexpr std::array<StreamResolutionPreset, 4> STREAM_RESOLUTION_PRESETS {{
      {640, 480},
      {720, 480},
      {1280, 720},
      {1920, 1080},
    }};

    bool is_1080i_mode(const VIDEO_MODE &videoMode) {
      return videoMode.width >= 1920 && videoMode.height >= 1080;
    }

    VIDEO_MODE make_stream_video_mode(const StreamResolutionPreset &preset, int bpp, int refresh) {
      return {preset.width, preset.height, bpp, refresh};
    }

    bool stream_resolutions_match(const VIDEO_MODE &left, const VIDEO_MODE &right) {
      return left.width == right.width && left.height == right.height;
    }

    bool is_smaller_video_mode(const VIDEO_MODE &left, const VIDEO_MODE &right) {
      const int leftArea = left.width * left.height;
      if (const int rightArea = right.width * right.height; leftArea != rightArea) {
        return leftArea < rightArea;
      }
      return left.width < right.width;
    }

    bool is_sd_wide_width_mode(const VIDEO_MODE &videoMode) {
      return videoMode.height <= 576 && videoMode.width > 640;
    }

  }  // namespace

  bool is_preferred_video_mode(const VIDEO_MODE &candidateVideoMode, const VIDEO_MODE &currentBestVideoMode) {
    if (is_1080i_mode(candidateVideoMode) && !is_1080i_mode(currentBestVideoMode) && currentBestVideoMode.width >= 1280 && currentBestVideoMode.height >= 720) {
      return false;
    }

    if (!is_1080i_mode(candidateVideoMode) && is_1080i_mode(currentBestVideoMode) && candidateVideoMode.width >= 1280 && candidateVideoMode.height >= 720) {
      return true;
    }

    if (candidateVideoMode.height < currentBestVideoMode.height) {
      return false;
    }

    if (candidateVideoMode.width < currentBestVideoMode.width) {
      return false;
    }

    if (candidateVideoMode.bpp < currentBestVideoMode.bpp) {
      return false;
    }

    if (candidateVideoMode.refresh < currentBestVideoMode.refresh) {
      return false;
    }

    return true;
  }

  VIDEO_MODE choose_best_video_mode(const std::vector<VIDEO_MODE> &availableVideoModes) {
    VIDEO_MODE bestVideoMode {0, 0, 0, 0};

    for (const VIDEO_MODE &videoMode : availableVideoModes) {
      if (is_preferred_video_mode(videoMode, bestVideoMode)) {
        bestVideoMode = videoMode;
      }
    }

    return bestVideoMode;
  }

  std::vector<VIDEO_MODE> stream_resolution_presets(int bpp, int refresh) {
    std::vector<VIDEO_MODE> presets;
    presets.reserve(STREAM_RESOLUTION_PRESETS.size());
    for (const StreamResolutionPreset &preset : STREAM_RESOLUTION_PRESETS) {
      presets.push_back(make_stream_video_mode(preset, bpp, refresh));
    }
    return presets;
  }

  std::vector<VIDEO_MODE> filter_stream_video_modes_for_encoder_settings(const std::vector<VIDEO_MODE> &availableVideoModes, unsigned long encoderSettings) {
    if ((encoderSettings & VIDEO_WIDESCREEN) != 0UL) {
      return availableVideoModes;
    }

    std::vector<VIDEO_MODE> filteredVideoModes;
    filteredVideoModes.reserve(availableVideoModes.size());
    for (const VIDEO_MODE &availableVideoMode : availableVideoModes) {
      if (!is_sd_wide_width_mode(availableVideoMode)) {
        filteredVideoModes.push_back(availableVideoMode);
      }
    }
    return filteredVideoModes;
  }

  VIDEO_MODE choose_default_stream_video_mode(const VIDEO_MODE &outputVideoMode) {
    const int bpp = outputVideoMode.bpp > 0 ? outputVideoMode.bpp : 32;
    const int refresh = outputVideoMode.refresh > 0 ? outputVideoMode.refresh : 60;

    return make_stream_video_mode({640, 480}, bpp, refresh);
  }

  VIDEO_MODE choose_default_stream_video_mode(const std::vector<VIDEO_MODE> &availableVideoModes, const VIDEO_MODE &outputVideoMode) {
    const VIDEO_MODE fallbackDefault = choose_default_stream_video_mode(outputVideoMode);
    for (const VIDEO_MODE &availableVideoMode : availableVideoModes) {
      if (stream_resolutions_match(availableVideoMode, fallbackDefault)) {
        return availableVideoMode;
      }
    }

    if (availableVideoModes.empty()) {
      return fallbackDefault;
    }

    VIDEO_MODE smallestVideoMode = availableVideoModes.front();
    for (const VIDEO_MODE &availableVideoMode : availableVideoModes) {
      if (is_smaller_video_mode(availableVideoMode, smallestVideoMode)) {
        smallestVideoMode = availableVideoMode;
      }
    }
    return smallestVideoMode;
  }

  VideoModeSelection select_best_video_mode(int bpp, int refresh) {
    VideoModeSelection selection {};

    VIDEO_MODE videoMode;
    void *position = nullptr;
    while (XVideoListModes(&videoMode, bpp, refresh, &position)) {
      selection.availableVideoModes.push_back(videoMode);
    }

    selection.bestVideoMode = choose_best_video_mode(selection.availableVideoModes);

    return selection;
  }

  std::vector<std::string> format_video_mode_lines(const VideoModeSelection &selection) {
    std::vector<std::string> lines;
    lines.reserve(selection.availableVideoModes.size() + 2U);
    lines.emplace_back("Available video modes:");
    for (const VIDEO_MODE &availableVideoMode : selection.availableVideoModes) {
      lines.push_back(
        "Width: " + std::to_string(availableVideoMode.width) + ", Height: " + std::to_string(availableVideoMode.height) + ", BPP: " + std::to_string(availableVideoMode.bpp) + ", Refresh: " + std::to_string(availableVideoMode.refresh)
      );
    }
    lines.push_back(
      "Best video mode: Width: " + std::to_string(selection.bestVideoMode.width) + ", Height: " + std::to_string(selection.bestVideoMode.height) + ", BPP: " + std::to_string(selection.bestVideoMode.bpp) + ", Refresh: " + std::to_string(selection.bestVideoMode.refresh)
    );
    return lines;
  }

  void log_video_modes(const VideoModeSelection &selection) {
    for (const std::string &line : format_video_mode_lines(selection)) {
      logging::info("video", line);
    }
  }

}  // namespace startup
