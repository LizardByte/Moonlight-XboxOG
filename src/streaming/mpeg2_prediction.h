/**
 * @file src/streaming/mpeg2_prediction.h
 * @brief Supplies a CPU oracle for progressive MPEG-2 luma prediction experiments.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace streaming {
  /**
   * @brief Read-only reference luma plane with explicit storage bounds.
   */
  struct Mpeg2LumaPlane {
    const std::uint8_t *data = nullptr;  ///< Reference pixels.
    std::size_t size = 0;  ///< Accessible byte count.
    int width = 0;  ///< Reference width.
    int height = 0;  ///< Reference height.
    int pitch = 0;  ///< Positive row stride.
  };

  /**
   * @brief Reconstruct progressive frame luma predictions with MPEG-2 rounding and edge extension.
   *
   * @param reference Decoded reference picture.
   * @param xHalfPel Horizontal source origin in half-pixel units, including motion.
   * @param yHalfPel Vertical source origin in half-pixel units, including motion.
   * @param width Prediction region width, up to 128 pixels.
   * @param height Prediction region height, up to 128 pixels.
   * @param destination Output pixels.
   * @param capacity Accessible output bytes.
   * @param pitch Output row stride.
   *
   * @return True when the complete prediction fits the supplied buffers.
   */
  bool predict_mpeg2_luma(const Mpeg2LumaPlane &reference, int xHalfPel, int yHalfPel, int width, int height, std::uint8_t *destination, std::size_t capacity, int pitch);

  /**
   * @brief Add inverse-transformed residuals with MPEG-2 byte saturation.
   *
   * @param prediction Packed prediction pixels, modified in place.
   * @param residual Signed residual samples for the same pixels.
   * @param count Number of accessible samples in both arrays.
   */
  void add_mpeg2_residual(std::uint8_t *prediction, const std::int16_t *residual, std::size_t count);
}  // namespace streaming
