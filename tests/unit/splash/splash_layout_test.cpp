// test include
#include "src/splash/splash_layout.h"

// lib includes
#include <gtest/gtest.h>

namespace {

  TEST(SplashLayoutTest, AppliesWidescreenCorrectionToStandardDefinitionModes) {
    const VIDEO_MODE videoMode {720, 480, 32, 60};

    EXPECT_NEAR(splash::get_display_aspect_ratio(videoMode, VIDEO_WIDESCREEN), 16.0f / 9.0f, 0.001f);
  }

  TEST(SplashLayoutTest, KeepsFramebufferAspectRatioWhenCorrectionIsNotNeeded) {
    const VIDEO_MODE videoMode {1280, 720, 32, 60};

    EXPECT_NEAR(splash::get_display_aspect_ratio(videoMode, VIDEO_WIDESCREEN), 1280.0f / 720.0f, 0.001f);
  }

  TEST(SplashLayoutTest, AppliesFourByThreeCorrectionToHighDefinitionModesWhenWidescreenIsDisabled) {
    const VIDEO_MODE videoMode {1280, 720, 32, 60};

    EXPECT_NEAR(splash::get_display_aspect_ratio(videoMode, 0UL), 4.0f / 3.0f, 0.001f);
  }

  TEST(SplashLayoutTest, CalculatesLogicalDisplayWidthFromTheEffectiveAspectRatio) {
    const VIDEO_MODE videoMode {1280, 720, 32, 60};

    EXPECT_EQ(splash::calculate_display_width(720, videoMode, 0UL), 960);
    EXPECT_EQ(splash::calculate_display_width(720, videoMode, VIDEO_WIDESCREEN), 1280);
  }

  TEST(SplashLayoutTest, ScalesAndCentersTheLogoInsideTheConfiguredBounds) {
    const VIDEO_MODE videoMode {640, 480, 32, 60};

    const splash::SplashLayout layout = splash::calculate_logo_destination(640, 480, 1000, 200, videoMode, 0UL);

    EXPECT_EQ(layout.width, 460);
    EXPECT_EQ(layout.height, 92);
    EXPECT_EQ(layout.x, 90);
    EXPECT_EQ(layout.y, 194);
  }

}  // namespace
