/**
 * @file experiments/mpeg2_motion_probe.cpp
 * @brief Compares CPU MPEG-2 half-pixel predictions with NV2A overlay sampling on a console.
 */
#include "src/streaming/mpeg2_prediction.h"
#include "src/streaming/pvideo_presenter.h"

#include <array>
#include <cstring>
#include <hal/debug.h>
#include <hal/video.h>
#include <pbkit/pbkit.h>
#include <windows.h>

namespace {
  constexpr int SOURCE_SIZE = 144;  ///< Reference plane includes a border for negative motion vectors.
  constexpr int BLOCK_SIZE = 128;  ///< An eight-by-eight grid of 16-pixel luma blocks.

  /**
   * @brief Draw CPU-computed reference values into the separate GPU scanout surface.
   *
   * @param prediction Packed reference predictions.
   */
  void draw_cpu_prediction(const std::array<std::uint8_t, BLOCK_SIZE * BLOCK_SIZE> &prediction) {
    for (int row = 0; row < BLOCK_SIZE; ++row) {
      for (int column = 0; column < BLOCK_SIZE; ++column) {
        const auto gray = static_cast<unsigned int>((prediction[row * BLOCK_SIZE + column] - 16) * 255 + 109) / 219U;
        pb_fill(64 + column, 100 + row, 1, 1, 0xff000000U | gray * 0x010101U);
      }
    }
  }
}  // namespace

/**
 * @brief Cycle integer and half-pixel motion cases for visual comparison on hardware.
 *
 * The overlay is composed during scanout and cannot be read back through the RGB
 * framebuffer. This experiment reports submission errors and displays the CPU
 * oracle alongside it; it does not certify bit-exact GPU reconstruction.
 * @return Nonzero if the output mode or overlay could not be initialized.
 */
int main() {
  if (!XVideoSetMode(640, 480, 32, REFRESH_DEFAULT)) {
    return 1;
  }
  static std::array<std::uint8_t, SOURCE_SIZE * SOURCE_SIZE> luma {};
  static std::array<std::uint8_t, SOURCE_SIZE * SOURCE_SIZE / 4> chroma {};
  static std::array<std::uint8_t, BLOCK_SIZE * BLOCK_SIZE> prediction {};
  chroma.fill(128);
  for (int row = 0; row < SOURCE_SIZE; ++row) {
    for (int column = 0; column < SOURCE_SIZE; ++column) {
      luma[row * SOURCE_SIZE + column] = static_cast<std::uint8_t>(16 + ((column * 17 + row * 29) % 220));
    }
  }
  const streaming::Yuv420FrameView frame {SOURCE_SIZE, SOURCE_SIZE, {luma.data(), chroma.data(), chroma.data()}, {luma.size(), chroma.size(), chroma.size()}, {SOURCE_SIZE, SOURCE_SIZE / 2, SOURCE_SIZE / 2}};
  const streaming::Mpeg2LumaPlane reference {luma.data(), luma.size(), SOURCE_SIZE, SOURCE_SIZE, SOURCE_SIZE};
  streaming::PvideoPresenter presenter;
  for (;;) {
    for (int motionY = -1; motionY <= 1; ++motionY) {
      for (int motionX = -1; motionX <= 1; ++motionX) {
        presenter.reset();
        std::memset(XVideoGetFB(), 0, 640 * 480 * 4);
        debugPrint("MPEG-2 prediction experiment\nCPU reference left | GPU sampling right\nMotion: %d/2, %d/2 pixels\nVisual comparison only; not a hardware decoder.\n", motionX, motionY);
        if (!streaming::predict_mpeg2_luma(reference, 16 + motionX, 16 + motionY, BLOCK_SIZE, BLOCK_SIZE, prediction.data(), prediction.size(), BLOCK_SIZE)) {
          return 2;
        }
        std::string error;
        if (!presenter.present(frame, {352, 100, BLOCK_SIZE, BLOCK_SIZE}, &error, {16 + motionX, 16 + motionY, BLOCK_SIZE, BLOCK_SIZE})) {
          debugPrint("GPU EXPERIMENT FAILED: %s\n", error.c_str());
          Sleep(10000);
          return 3;
        }
        // xemu composes PVIDEO only over a GPU-rendered scanout surface.
        // Draw the CPU oracle there as RGB; its prediction arithmetic and
        // range conversion remain independent of PVIDEO's YUV conversion.
        draw_cpu_prediction(prediction);
        pb_erase_text_screen();
        pb_print("MPEG-2 prediction experiment\nCPU reference left | GPU sampling right\nMotion: %d/2, %d/2 pixels\nVisual comparison only; not a hardware decoder.\n", motionX, motionY);
        pb_draw_text_screen();
        Sleep(5000);
      }
    }
  }
}
