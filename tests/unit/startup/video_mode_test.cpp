/**
 * @file tests/unit/startup/video_mode_test.cpp
 * @brief Verifies video mode selection helpers.
 */
// test includes
#include "src/startup/video_mode.h"

// standard includes
#include <vector>

// lib includes
#include <gtest/gtest.h>

namespace {

  TEST(VideoModeTest, ReturnsDefaultModeWhenNoModesAreAvailable) {
    const VIDEO_MODE bestVideoMode = startup::choose_best_video_mode({});

    EXPECT_EQ(bestVideoMode.width, 0);
    EXPECT_EQ(bestVideoMode.height, 0);
    EXPECT_EQ(bestVideoMode.bpp, 0);
    EXPECT_EQ(bestVideoMode.refresh, 0);
  }

  TEST(VideoModeTest, PrefersModesThatAreNotWorseInAnyDimension) {
    const std::vector<VIDEO_MODE> availableVideoModes = {
      {640, 480, 16, 60},
      {640, 480, 32, 60},
      {720, 480, 32, 60},
      {720, 480, 32, 75},
    };

    const VIDEO_MODE bestVideoMode = startup::choose_best_video_mode(availableVideoModes);

    EXPECT_EQ(bestVideoMode.width, 720);
    EXPECT_EQ(bestVideoMode.height, 480);
    EXPECT_EQ(bestVideoMode.bpp, 32);
    EXPECT_EQ(bestVideoMode.refresh, 75);
  }

  TEST(VideoModeTest, RejectsModesThatLoseWidthEvenWhenRefreshImproves) {
    const VIDEO_MODE currentBestVideoMode {1280, 720, 32, 60};
    const VIDEO_MODE candidateVideoMode {1024, 720, 32, 120};

    EXPECT_FALSE(startup::is_preferred_video_mode(candidateVideoMode, currentBestVideoMode));
  }

  TEST(VideoModeTest, Prefers720pOver1080iForHdStartupModes) {
    const std::vector<VIDEO_MODE> availableVideoModes = {
      {640, 480, 32, 60},
      {1280, 720, 32, 60},
      {1920, 1080, 32, 60},
    };

    const VIDEO_MODE bestVideoMode = startup::choose_best_video_mode(availableVideoModes);

    EXPECT_EQ(bestVideoMode.width, 1280);
    EXPECT_EQ(bestVideoMode.height, 720);
    EXPECT_EQ(bestVideoMode.bpp, 32);
    EXPECT_EQ(bestVideoMode.refresh, 60);
  }

}  // namespace
