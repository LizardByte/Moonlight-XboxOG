/**
 * @file src/streaming/video_codec_selection.cpp
 * @brief Maps client codec preferences to shared decoder capability bits.
 */
#include "src/streaming/video_codec_selection.h"

#include "third-party/moonlight-common-c/src/Limelight.h"

namespace streaming {
  int supported_video_formats(app::VideoCodecSelection selection) {
    switch (selection) {
      case app::VideoCodecSelection::h264:
        return VIDEO_FORMAT_H264;
      case app::VideoCodecSelection::mpeg2:
        return VIDEO_FORMAT_MPEG2;
      case app::VideoCodecSelection::h263p:
        return VIDEO_FORMAT_H263P;
      case app::VideoCodecSelection::autoDetect:
      default:
        return VIDEO_FORMAT_H264 | VIDEO_FORMAT_MPEG2 | VIDEO_FORMAT_H263P;
    }
  }
}  // namespace streaming
