#pragma once

// standard includes
#include <vector>

// local includes
#include "src/nxdk/hal/video.h"

namespace startup {

  struct VideoModeSelection {
    std::vector<VIDEO_MODE> availableVideoModes;
    VIDEO_MODE bestVideoMode;
  };

  VideoModeSelection select_best_video_mode(int bpp = 32, int refresh = REFRESH_DEFAULT);
  void log_video_modes(const VideoModeSelection &selection);

}  // namespace startup
