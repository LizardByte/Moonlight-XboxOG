// class header include
#include "src/startup/video_mode.h"

// local includes
#include "src/nxdk/hal/debug.h"

namespace startup {

  bool is_preferred_video_mode(const VIDEO_MODE &candidateVideoMode, const VIDEO_MODE &currentBestVideoMode) {
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

  void log_video_modes(const VideoModeSelection &selection) {
    debugPrint("Available video modes:\n");
    for (const VIDEO_MODE &availableVideoMode : selection.availableVideoModes) {
      debugPrint("Width: %d, Height: %d, BPP: %d, Refresh: %d\n", availableVideoMode.width, availableVideoMode.height, availableVideoMode.bpp, availableVideoMode.refresh);
    }

    debugPrint("Best video mode:\n");
    debugPrint("Width: %d, Height: %d, BPP: %d, Refresh: %d\n", selection.bestVideoMode.width, selection.bestVideoMode.height, selection.bestVideoMode.bpp, selection.bestVideoMode.refresh);
  }

}  // namespace startup
