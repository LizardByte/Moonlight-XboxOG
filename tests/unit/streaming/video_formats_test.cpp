/**
 * @file tests/unit/streaming/video_formats_test.cpp
 * @brief Regression coverage for shared codec negotiation and wire identifiers.
 */
#include <array>
#include <lizardbyte/common/testing.h>
#include <utility>

extern "C" {
#include "third-party/moonlight-common-c/src/VideoFormats.h"
}

namespace {
  constexpr char ALL_CODECS[] = "AV1/90000\nsprop-parameter-sets=AAAAAU\nMPV/90000\nH263-1998/90000\n";  ///< Host SDP advertising every codec.
  constexpr int ALL_FORMATS = VIDEO_FORMAT_MASK_AV1 | VIDEO_FORMAT_MASK_H265 | VIDEO_FORMAT_MASK_H264 | VIDEO_FORMAT_MPEG2 | VIDEO_FORMAT_H263P;  ///< All known decoder formats.
  constexpr int ALL_MODES = SCM_MASK_AV1 | SCM_MASK_HEVC | SCM_MASK_H264 | SCM_MPEG2 | SCM_H263P;  ///< All known host codec capabilities.
}  // namespace

/**
 * @brief Prefer each modern codec profile before either legacy codec.
 */
TEST(VideoFormatNegotiation, PreservesModernCodecPriorityWhenLegacyCodecsAreAvailable) {
  const std::array order = {VIDEO_FORMAT_AV1_HIGH10_444, VIDEO_FORMAT_AV1_MAIN10, VIDEO_FORMAT_AV1_HIGH8_444, VIDEO_FORMAT_AV1_MAIN8, VIDEO_FORMAT_H265_REXT10_444, VIDEO_FORMAT_H265_MAIN10, VIDEO_FORMAT_H265_REXT8_444, VIDEO_FORMAT_H265, VIDEO_FORMAT_H264_HIGH8_444, VIDEO_FORMAT_H264, VIDEO_FORMAT_MPEG2, VIDEO_FORMAT_H263P};
  int supported = ALL_FORMATS;
  for (int expected : order) {
    EXPECT_EQ(chooseVideoFormat(supported, ALL_MODES, ALL_CODECS), expected);
    supported &= ~expected;
  }
  EXPECT_EQ(chooseVideoFormat(supported, ALL_MODES, ALL_CODECS), 0);
}

/**
 * @brief Select only mutually supported profiles from the client's strict request mask.
 */
TEST(VideoFormatNegotiation, ExplicitRequestsNeverFallBackToAnUnsupportedCodec) {
  for (int format : {VIDEO_FORMAT_H264, VIDEO_FORMAT_H265, VIDEO_FORMAT_AV1_MAIN8, VIDEO_FORMAT_MPEG2, VIDEO_FORMAT_H263P}) {
    EXPECT_EQ(chooseVideoFormat(format, ALL_MODES, ALL_CODECS), format);
  }
  EXPECT_EQ(chooseVideoFormat(VIDEO_FORMAT_MPEG2, SCM_H264, ALL_CODECS), 0);
  EXPECT_EQ(chooseVideoFormat(VIDEO_FORMAT_H263P, SCM_H264, ALL_CODECS), 0);
  EXPECT_EQ(chooseVideoFormat(VIDEO_FORMAT_AV1_MAIN10, SCM_AV1_MAIN8, ALL_CODECS), 0);
  EXPECT_EQ(chooseVideoFormat(VIDEO_FORMAT_H265_MAIN10, SCM_HEVC, ALL_CODECS), 0);
  EXPECT_EQ(chooseVideoFormat(VIDEO_FORMAT_H264_HIGH8_444, SCM_H264, ALL_CODECS), 0);
}

/**
 * @brief Require matching host capabilities and SDP markers for legacy codecs.
 */
TEST(VideoFormatNegotiation, RequiresHostMetadataAndSdpForLegacyCodecs) {
  const std::array aliases = {
    std::pair {VIDEO_FORMAT_MPEG2, "MPV/90000"},
    std::pair {VIDEO_FORMAT_MPEG2, "MPEG2/90000"},
    std::pair {VIDEO_FORMAT_MPEG2, "MPEG-2/90000"},
    std::pair {VIDEO_FORMAT_H263P, "H263-1998/90000"},
    std::pair {VIDEO_FORMAT_H263P, "H263-2000/90000"},
  };
  for (const auto &[format, sdp] : aliases) {
    EXPECT_EQ(chooseVideoFormat(format, ALL_MODES, sdp), format);
    EXPECT_EQ(chooseVideoFormat(format, 0, sdp), 0);
    EXPECT_EQ(chooseVideoFormat(format, ALL_MODES, ""), 0);
  }
  EXPECT_EQ(chooseVideoFormat(ALL_FORMATS, ALL_MODES, nullptr), 0);
}

/**
 * @brief Preserve H.264 for old hosts while respecting explicit legacy-only advertisements.
 */
TEST(VideoFormatNegotiation, SupportsLegacyHostsAndLegacyOnlyHosts) {
  EXPECT_EQ(chooseVideoFormat(ALL_FORMATS, 0, ""), VIDEO_FORMAT_H264);
  EXPECT_EQ(chooseVideoFormat(ALL_FORMATS, 0, "sprop-parameter-sets=AAAAAU"), VIDEO_FORMAT_H265);
  EXPECT_EQ(chooseVideoFormat(ALL_FORMATS, 0, ALL_CODECS), VIDEO_FORMAT_AV1_MAIN8);
  EXPECT_EQ(chooseVideoFormat(VIDEO_FORMAT_H265_MAIN10, 0, ALL_CODECS), 0);
  EXPECT_EQ(chooseVideoFormat(VIDEO_FORMAT_AV1_MAIN10, 0, ALL_CODECS), 0);
  EXPECT_EQ(chooseVideoFormat(ALL_FORMATS, SCM_MPEG2, ALL_CODECS), VIDEO_FORMAT_MPEG2);
  EXPECT_EQ(chooseVideoFormat(ALL_FORMATS, SCM_H263P, ALL_CODECS), VIDEO_FORMAT_H263P);
  EXPECT_EQ(chooseVideoFormat(ALL_FORMATS, SCM_HEVC, ""), 0);
  EXPECT_EQ(chooseVideoFormat(ALL_FORMATS, SCM_AV1_MAIN8, ""), 0);
}

/**
 * @brief Map every known codec profile and reject combinations and reserved bits.
 */
TEST(VideoFormatNegotiation, EncodesOnlyValidSingleNegotiatedFormats) {
  const std::array mappings = {
    std::pair {VIDEO_FORMAT_H264, 0},
    std::pair {VIDEO_FORMAT_H264_HIGH8_444, 0},
    std::pair {VIDEO_FORMAT_H265, 1},
    std::pair {VIDEO_FORMAT_H265_MAIN10, 1},
    std::pair {VIDEO_FORMAT_H265_REXT8_444, 1},
    std::pair {VIDEO_FORMAT_H265_REXT10_444, 1},
    std::pair {VIDEO_FORMAT_AV1_MAIN8, 2},
    std::pair {VIDEO_FORMAT_AV1_MAIN10, 2},
    std::pair {VIDEO_FORMAT_AV1_HIGH8_444, 2},
    std::pair {VIDEO_FORMAT_AV1_HIGH10_444, 2},
    std::pair {VIDEO_FORMAT_MPEG2, 3},
    std::pair {VIDEO_FORMAT_H263P, 4},
  };
  for (const auto &[format, wire] : mappings) {
    EXPECT_EQ(getVideoBitStreamFormat(format), wire);
  }
  for (int invalid : {0, -1, 0x2, 0x8, 0x40000000, VIDEO_FORMAT_H264 | VIDEO_FORMAT_MPEG2}) {
    EXPECT_EQ(getVideoBitStreamFormat(invalid), -1);
  }
}
