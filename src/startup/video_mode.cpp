// class header include
#include "src/startup/video_mode.h"

// local includes
#include "src/nxdk/hal/debug.h"

namespace startup {

  VideoModeSelection select_best_video_mode(int bpp, int refresh) {
    VideoModeSelection selection {};
    selection.bestVideoMode = {0, 0, 0, 0};

    VIDEO_MODE videoMode;
    void *position = nullptr;
    while (XVideoListModes(&videoMode, bpp, refresh, &position)) {
      selection.availableVideoModes.push_back(videoMode);

      if (videoMode.height < selection.bestVideoMode.height) {
        continue;
      }

      if (videoMode.width < selection.bestVideoMode.width) {
        continue;
      }

      if (videoMode.bpp < selection.bestVideoMode.bpp) {
        continue;
      }

      if (videoMode.refresh < selection.bestVideoMode.refresh) {
        continue;
      }

      selection.bestVideoMode = videoMode;
    }

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
