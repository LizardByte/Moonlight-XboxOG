/**
 * @file tests/unit/streaming/video_overlay_test.cpp
 * @brief Verifies equivalent GPU/software panel pixels and destination bounds.
 */
// test includes
// test includes
#include <lizardbyte/common/testing.h>

// standard includes
#include <array>
#include <cstring>

// local includes
#include "src/streaming/video_overlay.h"

/**
 * @brief YUY2 drawing leaves padding untouched and uses limited-range neutral grayscale.
 */
TEST(VideoOverlayTest, CopiesLimitedRangePanelIntoPackedVideo) {
  streaming::VideoOverlay overlay;
  overlay.set_bitmap(2, 2, {0, 255, 128, 64});
  std::array<std::uint8_t, 16 * 40> output;
  output.fill(0xcc);
  ASSERT_TRUE(overlay.draw_yuy2(output.data(), output.size(), 40, 16, 16, {0, 0, 16, 16}));
  EXPECT_EQ(output[8 * 40 + 8 * 2], 16);
  EXPECT_EQ(output[8 * 40 + 8 * 2 + 1], 128);
  EXPECT_EQ(output[8 * 40 + 9 * 2], 235);
  EXPECT_EQ(output[9 * 40 + 8 * 2], 126);
  EXPECT_EQ(output[9 * 40 + 9 * 2], 71);
  EXPECT_EQ(output[8 * 40 + 7 * 2], 0xcc);
  EXPECT_EQ(output[8 * 40 + 10 * 2], 0xcc);
  EXPECT_EQ(output[8 * 40 + 39], 0xcc);
}

/**
 * @brief Software output uses the same grayscale panel in every Xbox framebuffer format.
 */
TEST(VideoOverlayTest, DrawsGrayscaleInAllFramebufferFormats) {
  streaming::VideoOverlay overlay;
  overlay.set_bitmap(2, 1, {0, 255});
  for (const int bpp : {15, 16, 32}) {
    const int bytes = bpp == 32 ? 4 : 2;
    std::vector<std::uint8_t> output(20 * 20 * bytes, 0xcc);
    ASSERT_TRUE(overlay.draw_rgb(output.data(), output.size(), 20 * bytes, 20, 20, bpp, {0, 0, 20, 20}));
    std::uint32_t black = 0;
    std::uint32_t white = 0;
    std::memcpy(&black, output.data() + (8 * 20 + 8) * bytes, bytes);
    std::memcpy(&white, output.data() + (8 * 20 + 9) * bytes, bytes);
    EXPECT_EQ(black, bpp == 32 ? 0xff000000U : (bpp == 15 ? 0x8000U : 0U));
    EXPECT_EQ(white, bpp == 32 ? 0xffffffffU : 0xffffU);
    EXPECT_EQ(output[0], 0xcc);
  }
}

/**
 * @brief Scaling is cached in source coordinates and refreshed when text or geometry changes.
 */
TEST(VideoOverlayTest, RefreshesScaledCacheAndHandlesTinyFrames) {
  streaming::VideoOverlay overlay;
  overlay.set_bitmap(2, 1, {0, 255});
  std::vector<std::uint8_t> output(64 * 64 * 2, 0xcc);
  ASSERT_TRUE(overlay.draw_yuy2(output.data(), output.size(), 128, 64, 64, {0, 0, 32, 32}));
  EXPECT_EQ(output[16 * 128 + 16 * 2], 16);
  EXPECT_EQ(output[16 * 128 + 18 * 2], 235);
  overlay.set_bitmap(2, 1, {255, 0});
  ASSERT_TRUE(overlay.draw_yuy2(output.data(), output.size(), 128, 64, 64, {0, 0, 32, 32}));
  EXPECT_EQ(output[16 * 128 + 16 * 2], 235);
  EXPECT_EQ(output[16 * 128 + 18 * 2], 16);
  std::array<std::uint8_t, 8> tiny {};
  EXPECT_TRUE(overlay.draw_yuy2(tiny.data(), tiny.size(), 4, 2, 2, {0, 0, 9, 9}));
  EXPECT_TRUE(overlay.draw_yuy2(tiny.data(), tiny.size(), 4, 2, 2, {0, 0, 9, 9}));
}

/**
 * @brief Invalid storage never changes the destination and malformed bitmaps clear the panel.
 */
TEST(VideoOverlayTest, RejectsTruncatedStorageAndClearsInvalidBitmaps) {
  streaming::VideoOverlay overlay;
  overlay.set_bitmap(2, 2, {0, 255, 0, 255});
  std::array<std::uint8_t, 512> output;
  output.fill(0xcc);
  EXPECT_FALSE(overlay.draw_yuy2(output.data(), output.size() - 1, 32, 16, 16, {0, 0, 16, 16}));
  EXPECT_FALSE(overlay.draw_rgb(output.data(), output.size(), 32, 16, 16, 24, {0, 0, 16, 16}));
  EXPECT_FALSE(overlay.draw_rgb(output.data(), output.size(), 32, 16, 16, 16, {-1, 0, 16, 16}));
  for (auto byte : output) {
    EXPECT_EQ(byte, 0xcc);
  }
  overlay.set_bitmap(2, 2, {1});
  EXPECT_EQ(overlay.width(), 0);
  EXPECT_TRUE(overlay.draw_rgb(nullptr, 0, 0, 0, 0, 0, {}));
}
