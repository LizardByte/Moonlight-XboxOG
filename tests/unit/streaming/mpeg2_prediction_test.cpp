/**
 * @file tests/unit/streaming/mpeg2_prediction_test.cpp
 * @brief Checks MPEG-2 half-pixel rounding, negative motion and residual saturation.
 */
// test includes
// test includes
#include <lizardbyte/common/testing.h>

// standard includes
#include <array>

// local includes
#include "src/streaming/mpeg2_prediction.h"

/**
 * @brief Horizontal, vertical and diagonal prediction use the specified integer rounding.
 */
TEST(Mpeg2PredictionTest, RoundsHalfPixelPredictions) {
  const std::array<std::uint8_t, 4> pixels {0, 1, 2, 4};
  const streaming::Mpeg2LumaPlane reference {pixels.data(), pixels.size(), 2, 2, 2};
  std::uint8_t prediction = 0;
  ASSERT_TRUE(streaming::predict_mpeg2_luma(reference, 1, 0, 1, 1, &prediction, 1, 1));
  EXPECT_EQ(prediction, 1);
  ASSERT_TRUE(streaming::predict_mpeg2_luma(reference, 0, 1, 1, 1, &prediction, 1, 1));
  EXPECT_EQ(prediction, 1);
  ASSERT_TRUE(streaming::predict_mpeg2_luma(reference, 1, 1, 1, 1, &prediction, 1, 1));
  EXPECT_EQ(prediction, 2);
}

/**
 * @brief Negative motion extends picture edges instead of wrapping or reading outside storage.
 */
TEST(Mpeg2PredictionTest, ExtendsEdgesForNegativeMotion) {
  const std::array<std::uint8_t, 4> pixels {20, 40, 60, 80};
  const streaming::Mpeg2LumaPlane reference {pixels.data(), pixels.size(), 2, 2, 2};
  std::array<std::uint8_t, 4> prediction {};
  ASSERT_TRUE(streaming::predict_mpeg2_luma(reference, -1, -1, 2, 2, prediction.data(), prediction.size(), 2));
  EXPECT_EQ(prediction, (std::array<std::uint8_t, 4> {20, 30, 40, 50}));
  EXPECT_FALSE(streaming::predict_mpeg2_luma(reference, 0, 0, 2, 2, prediction.data(), 3, 2));
}

/**
 * @brief Signed inverse-transformed samples saturate to the valid reconstructed byte range.
 */
TEST(Mpeg2PredictionTest, ClipsResidualAddition) {
  std::array<std::uint8_t, 4> prediction {0, 255, 100, 100};
  const std::array<std::int16_t, 4> residual {-32768, 32767, -70, 90};
  streaming::add_mpeg2_residual(prediction.data(), residual.data(), prediction.size());
  EXPECT_EQ(prediction, (std::array<std::uint8_t, 4> {0, 255, 30, 190}));
}
