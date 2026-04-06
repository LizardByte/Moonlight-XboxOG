#pragma once

// standard includes
#include <string>
#include <vector>

// nxdk includes
#include <hal/video.h>

namespace startup {

  /**
   * @brief Represents the set of available video modes and the chosen best mode.
   *
   * This structure is used during startup to collect detected video modes supported
   * by the hardware and to store the best candidate selected by the detection
   * algorithm.
   */
  struct VideoModeSelection {
    /**
     * @brief List of detected, available video modes.
     *
     * The vector contains VIDEO_MODE entries returned by the platform video
     * subsystem. The detection code fills this list and then chooses a
     * preferred mode to populate @c bestVideoMode.
     */
    std::vector<VIDEO_MODE> availableVideoModes;

    /**
     * @brief The selected best video mode from @c availableVideoModes.
     *
     * This is the mode chosen by the selection logic as the most suitable
     * for the current configuration (color depth, refresh rate, etc.). If no
     * suitable mode could be chosen, this value may be left as the default
     * VIDEO_MODE value.
     */
    VIDEO_MODE bestVideoMode;
  };

  /**
   * @brief Return whether a candidate mode should replace the current best mode.
   *
   * A candidate must be at least as good as the current best mode in width,
   * height, color depth, and refresh rate.
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
   * @return The preferred mode, or a default-initialized VIDEO_MODE when the
   *         input list is empty.
   */
  VIDEO_MODE choose_best_video_mode(const std::vector<VIDEO_MODE> &availableVideoModes);

  /**
   * @brief Detect and choose the best available video mode.
   *
   * @param bpp Desired bits-per-pixel (color depth). Default is 32.
   * @param refresh Desired refresh rate or REFRESH_DEFAULT to accept default.
   * @return A VideoModeSelection containing the list of available modes and the
   *         selected best mode.
   */
  VideoModeSelection select_best_video_mode(int bpp = 32, int refresh = REFRESH_DEFAULT);

  std::vector<std::string> format_video_mode_lines(const VideoModeSelection &selection);

  /**
   * @brief Log information about available and selected video modes.
   *
   * This function emits diagnostic information (e.g., to console or platform
   * log) about the contents of the provided VideoModeSelection.
   *
   * @param selection The selection object to log.
   */
  void log_video_modes(const VideoModeSelection &selection);

}  // namespace startup
