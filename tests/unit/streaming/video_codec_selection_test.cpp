/**
 * @file tests/unit/streaming/video_codec_selection_test.cpp
 * @brief Checks automatic capabilities and strict codec requests.
 */
#include "src/streaming/video_codec_selection.h"
#include "third-party/moonlight-common-c/src/Limelight.h"

#include <lizardbyte/common/testing.h>

/**
 * @brief Automatic mode advertises every decoder implemented by this client.
 */
TEST(VideoCodecSelectionTest, AutomaticModeAdvertisesSupportedCodecs) {
  const int formats = VIDEO_FORMAT_H264 | VIDEO_FORMAT_MPEG2 | VIDEO_FORMAT_H263P;
  EXPECT_EQ(streaming::supported_video_formats(app::VideoCodecSelection::autoDetect), formats);
  EXPECT_EQ(streaming::supported_video_formats(static_cast<app::VideoCodecSelection>(-1)), formats);
}

/**
 * @brief Explicit choices cannot negotiate a different codec as a fallback.
 */
TEST(VideoCodecSelectionTest, ExplicitChoicesAdvertiseOnlyTheRequestedCodec) {
  EXPECT_EQ(streaming::supported_video_formats(app::VideoCodecSelection::h264), VIDEO_FORMAT_H264);
  EXPECT_EQ(streaming::supported_video_formats(app::VideoCodecSelection::mpeg2), VIDEO_FORMAT_MPEG2);
  EXPECT_EQ(streaming::supported_video_formats(app::VideoCodecSelection::h263p), VIDEO_FORMAT_H263P);
}
