/**
 * @file experiments/pvideo_presentation_probe.cpp
 * @brief Exercises GPU overlay colors, buffer updates and restoration independently of networking and input.
 */
#include "src/streaming/pvideo_presenter.h"
#include "src/streaming/stats_overlay.h"

#include <array>
#include <cstring>
#include <hal/debug.h>
#include <hal/video.h>
#include <windows.h>

namespace {
  constexpr int WIDTH = 1280;  ///< HD source width exercises full-size DMA buffer rotation.
  constexpr int HEIGHT = 720;  ///< HD source height exercises downscaling to the probe output.

  /**
   * @brief Fill limited-range BT.601 red, green, blue and white bars.
   *
   * @param y Luma storage.
   * @param u Blue-difference chroma storage.
   * @param v Red-difference chroma storage.
   * @param phase Rotation of the four color bars.
   */
  void fill_bars(std::array<std::uint8_t, WIDTH * HEIGHT> &y, std::array<std::uint8_t, WIDTH * HEIGHT / 4> &u, std::array<std::uint8_t, WIDTH * HEIGHT / 4> &v, int phase) {
    constexpr std::array<std::array<std::uint8_t, 3>, 4> colors {{{81, 90, 240}, {145, 54, 34}, {41, 240, 110}, {235, 128, 128}}};
    for (int row = 0; row < HEIGHT; ++row) {
      for (int column = 0; column < WIDTH; ++column) {
        y[row * WIDTH + column] = colors[(column / (WIDTH / 4) + phase) % 4][0];
      }
    }
    for (int row = 0; row < HEIGHT / 2; ++row) {
      for (int column = 0; column < WIDTH / 2; ++column) {
        const auto &color = colors[(column / (WIDTH / 8) + phase) % 4];
        u[row * (WIDTH / 2) + column] = color[1];
        v[row * (WIDTH / 2) + column] = color[2];
      }
    }
  }
}  // namespace

/**
 * @brief Run two overlay sessions with visible color changes, then leave the restored framebuffer visible.
 *
 * @return Nonzero if mode setup, overlay submission or teardown fails.
 */
int main() {
  if (!XVideoSetMode(640, 480, 32, REFRESH_DEFAULT)) {
    return 1;
  }
  std::memset(XVideoGetFB(), 0x40, 640 * 480 * 4);
  debugResetCursor();
  streaming::StreamStatisticsSnapshot statistics {};
  statistics.width = 1280;
  statistics.height = 720;
  statistics.fps = 30;
  statistics.incomingFps = 29.96;
  statistics.decodingFps = 18.24;
  statistics.renderingFps = 18.0;
  statistics.decodeTimeMs = 41.35;
  statistics.presentationTimeMs = 2.5;
  statistics.videoBitrateMbps = 4.2;
  statistics.hostProcessingMs = 6.8;
  statistics.presentationPath = "GPU";
  statistics.videoCodec = "H.264";
  statistics.decoderFeatures = "MMX/SSE";
  const auto statisticLines = streaming::build_live_stats_overlay_lines(statistics);
  if (statisticLines[1] != "FPS: incoming 30.0 | decoded 18.2 | rendered 18.0") {
    debugPrint("STATISTICS FORMAT FAILED\n");
    for (;;) {
      Sleep(1000);
    }
  }
  debugPrint("STATISTICS NUMERIC FORMAT PASSED\n%s\n", statisticLines[1].c_str());
  debugPrint("CPU FRAMEBUFFER BASELINE\nGPU test begins after 2 seconds.\n");
  Sleep(2000);
  static std::array<std::uint8_t, WIDTH * HEIGHT> y {};
  static std::array<std::uint8_t, WIDTH * HEIGHT / 4> u {};
  static std::array<std::uint8_t, WIDTH * HEIGHT / 4> v {};
  const streaming::Yuv420FrameView frame {WIDTH, HEIGHT, {y.data(), u.data(), v.data()}, {y.size(), u.size(), v.size()}, {WIDTH, WIDTH / 2, WIDTH / 2}};
  streaming::PvideoPresenter presenter;
  for (int session = 0; session < 2; ++session) {
    for (int number = 0; number < 180; ++number) {
      fill_bars(y, u, v, number / 30);
      std::string error;
      if (!presenter.present(frame, {64, 144, 512, 288}, &error)) {
        debugResetCursor();
        debugPrint("PVIDEO PROBE FAILED\n%s\n", error.c_str());
        Sleep(20000);
        return 2;
      }
      Sleep(33);
    }
    if (!presenter.reset()) {
      debugResetCursor();
      debugPrint("PVIDEO TEARDOWN FAILED\n");
      Sleep(20000);
      return 3;
    }
    std::memset(XVideoGetFB(), 0x30, 640 * 480 * 4);
    debugResetCursor();
    debugPrint("Overlay stopped; CPU framebuffer restored.\nSession %d completed.\n", session + 1);
    Sleep(2000);
  }
  std::memset(XVideoGetFB(), 0x30, 640 * 480 * 4);
  debugResetCursor();
  debugPrint("PVIDEO PROBE COMPLETE\nBoth overlay sessions stopped and restored the framebuffer.\n");
  for (;;) {
    Sleep(1000);
  }
}
