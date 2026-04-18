/**
 * @file src/startup/video_mode.cpp
 * @brief Implements video mode selection helpers.
 */
// class header include
#include "src/startup/video_mode.h"

// local includes
#include "src/logging/logger.h"

namespace startup {

  namespace {

    bool is_1080i_mode(const VIDEO_MODE &videoMode) {
      return videoMode.width >= 1920 && videoMode.height >= 1080;
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
