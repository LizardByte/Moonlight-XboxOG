// local includes
#include "src/nxdk/windows.h"
#include "src/splash/splash_screen.h"
#include "src/startup/memory_stats.h"
#include "src/startup/video_mode.h"

int main() {
  const startup::VideoModeSelection videoModeSelection = startup::select_best_video_mode();
  const VIDEO_MODE &bestVideoMode = videoModeSelection.bestVideoMode;

  XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);

  startup::log_video_modes(videoModeSelection);
  startup::log_memory_statistics();

  Sleep(10000);

  XVideoSetMode(bestVideoMode.width, bestVideoMode.height, bestVideoMode.bpp, bestVideoMode.refresh);

  splash::show_splash_screen(bestVideoMode);
  return 0;
}
