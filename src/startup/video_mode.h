/**
 * @file src/startup/video_mode.h
 * @brief Declares video mode selection helpers.
 */
#pragma once

// standard includes
#include <string>
#include <vector>

// nxdk includes
#include <hal/video.h>

namespace startup {

  /**
   * @brief Represents the set of available video modes and the chosen best mode.
   */
  struct VideoModeSelection {
    std::vector<VIDEO_MODE> availableVideoModes;  ///< Detected video modes supported by the platform.
    VIDEO_MODE bestVideoMode {};  ///< Best candidate selected from the detected modes.
  };

  /**
   * @brief Return whether a candidate mode should replace the current best mode.
   *
   * A candidate must be at least as good as the current best mode in width,
   * height, color depth, and refresh rate, while preferring 720p over 1080i.
   *
   * @param candidateVideoMode The mode being evaluated.
   * @param currentBestVideoMode The currently selected best mode.
   * @return true if the candidate should replace the current best mode.
   */
  bool is_preferred_video_mode(const VIDEO_MODE &candidateVideoMode, const VIDEO_MODE &currentBestVideoMode);

  /**
   * @brief Choose the best video mode from an already collected list.
   *
   * @param availableVideoModes The list of candidate modes to evaluate.
   * @return The preferred mode, or a default-initialized VIDEO_MODE when the input list is empty.
   */
  VIDEO_MODE choose_best_video_mode(const std::vector<VIDEO_MODE> &availableVideoModes);

  /**
   * @brief Return the fixed stream-resolution presets exposed in the settings UI.
   *
   * These presets are independent from the Xbox output modes returned by
   * `XVideoListModes()`. They define only the host stream resolution that
   * Moonlight requests when starting a session.
   *
   * @param bpp Bits-per-pixel metadata to attach to each preset.
   * @param refresh Refresh-rate metadata to attach to each preset.
   * @return Ordered list of stream-resolution presets.
   */
  std::vector<VIDEO_MODE> stream_resolution_presets(int bpp = 32, int refresh = 60);

  /**
   * @brief Choose the default stream-resolution preset for the current output mode.
   *
   * The shell output mode still comes from Xbox video-mode detection, but stream
   * quality is controlled separately through the settings presets.
   *
   * @param outputVideoMode Active Xbox output mode selected at startup.
   * @return Default stream-resolution preset for new or missing settings.
   */
  VIDEO_MODE choose_default_stream_video_mode(const VIDEO_MODE &outputVideoMode);

  /**
   * @brief Detect and choose the best available video mode.
   *
   * @param bpp Desired bits-per-pixel color depth. Default is 32.
   * @param refresh Desired refresh rate or REFRESH_DEFAULT to accept the default.
   * @return A VideoModeSelection containing the detected modes and the selected best mode.
   */
  VideoModeSelection select_best_video_mode(int bpp = 32, int refresh = REFRESH_DEFAULT);

  /**
   * @brief Return human-readable lines describing the detected and selected video modes.
   *
   * @param selection Video-mode selection to describe.
   * @return Formatted diagnostic lines for startup logging or UI display.
   */
  std::vector<std::string> format_video_mode_lines(const VideoModeSelection &selection);

  /**
   * @brief Log information about available and selected video modes.
   *
   * @param selection The selection object to log.
   */
  void log_video_modes(const VideoModeSelection &selection);

}  // namespace startup
