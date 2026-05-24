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

  TEST(VideoModeTest, ExposesOnlyXboxVideoModeStreamChoicesAsFallbacks) {
    const std::vector<VIDEO_MODE> presets = startup::stream_resolution_presets(32, 60);

    ASSERT_EQ(presets.size(), 4U);
    EXPECT_EQ(presets.front().width, 640);
    EXPECT_EQ(presets.front().height, 480);
    EXPECT_EQ(presets[1].width, 720);
    EXPECT_EQ(presets[1].height, 480);
    EXPECT_EQ(presets[2].width, 1280);
    EXPECT_EQ(presets[2].height, 720);
    EXPECT_EQ(presets.back().width, 1920);
    EXPECT_EQ(presets.back().height, 1080);
    EXPECT_EQ(presets.back().bpp, 32);
    EXPECT_EQ(presets.back().refresh, 60);
  }

  TEST(VideoModeTest, FiltersSdWideWidthModesWhenWidescreenIsDisabled) {
    const std::vector<VIDEO_MODE> availableVideoModes = {
      {640, 480, 32, 60},
      {720, 480, 32, 60},
      {1280, 720, 32, 60},
    };

    const std::vector<VIDEO_MODE> filteredVideoModes = startup::filter_stream_video_modes_for_encoder_settings(availableVideoModes, 0UL);

    ASSERT_EQ(filteredVideoModes.size(), 2U);
    EXPECT_EQ(filteredVideoModes[0].width, 640);
    EXPECT_EQ(filteredVideoModes[0].height, 480);
    EXPECT_EQ(filteredVideoModes[1].width, 1280);
    EXPECT_EQ(filteredVideoModes[1].height, 720);
  }

  TEST(VideoModeTest, KeepsSdWideWidthModesWhenWidescreenIsEnabled) {
    const std::vector<VIDEO_MODE> availableVideoModes = {
      {640, 480, 32, 60},
      {720, 480, 32, 60},
      {1280, 720, 32, 60},
    };

    const std::vector<VIDEO_MODE> filteredVideoModes = startup::filter_stream_video_modes_for_encoder_settings(availableVideoModes, VIDEO_WIDESCREEN);

    ASSERT_EQ(filteredVideoModes.size(), 3U);
    EXPECT_EQ(filteredVideoModes[1].width, 720);
    EXPECT_EQ(filteredVideoModes[1].height, 480);
  }

  TEST(VideoModeTest, ChoosesSdXboxModeForHdStreamDefaults) {
    const VIDEO_MODE defaultPreset = startup::choose_default_stream_video_mode({1280, 720, 32, 60});

    EXPECT_EQ(defaultPreset.width, 640);
    EXPECT_EQ(defaultPreset.height, 480);
    EXPECT_EQ(defaultPreset.bpp, 32);
    EXPECT_EQ(defaultPreset.refresh, 60);
  }

  TEST(VideoModeTest, ChoosesSdXboxModeFor60HzOutputModes) {
    const VIDEO_MODE defaultPreset = startup::choose_default_stream_video_mode({640, 480, 32, 60});

    EXPECT_EQ(defaultPreset.width, 640);
    EXPECT_EQ(defaultPreset.height, 480);
    EXPECT_EQ(defaultPreset.bpp, 32);
    EXPECT_EQ(defaultPreset.refresh, 60);
  }

  TEST(VideoModeTest, ChoosesSdXboxModeFor50HzOutputModes) {
    const VIDEO_MODE defaultPreset = startup::choose_default_stream_video_mode({640, 480, 32, 50});

    EXPECT_EQ(defaultPreset.width, 640);
    EXPECT_EQ(defaultPreset.height, 480);
    EXPECT_EQ(defaultPreset.bpp, 32);
    EXPECT_EQ(defaultPreset.refresh, 50);
  }

  TEST(VideoModeTest, ChoosesDetectedSdModeWhenAvailable) {
    const std::vector<VIDEO_MODE> availableVideoModes = {
      {720, 480, 32, 60},
      {640, 480, 16, 60},
      {1280, 720, 32, 60},
    };

    const VIDEO_MODE defaultPreset = startup::choose_default_stream_video_mode(availableVideoModes, {1280, 720, 32, 60});

    EXPECT_EQ(defaultPreset.width, 640);
    EXPECT_EQ(defaultPreset.height, 480);
    EXPECT_EQ(defaultPreset.bpp, 16);
    EXPECT_EQ(defaultPreset.refresh, 60);
  }

  TEST(VideoModeTest, ChoosesSmallestDetectedModeWhenSdModeIsUnavailable) {
    const std::vector<VIDEO_MODE> availableVideoModes = {
      {1280, 720, 32, 60},
      {720, 480, 32, 60},
    };

    const VIDEO_MODE defaultPreset = startup::choose_default_stream_video_mode(availableVideoModes, {1280, 720, 32, 60});

    EXPECT_EQ(defaultPreset.width, 720);
    EXPECT_EQ(defaultPreset.height, 480);
    EXPECT_EQ(defaultPreset.bpp, 32);
    EXPECT_EQ(defaultPreset.refresh, 60);
  }

}  // namespace
