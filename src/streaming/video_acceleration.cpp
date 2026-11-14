/**
 * @file src/streaming/video_acceleration.cpp
 * @brief Implements safe overlay packing and stream acceleration diagnostics.
 */
#include "src/streaming/video_acceleration.h"

namespace streaming {
  std::string overlay_frame_error(const Yuv420FrameView &frame) {
    if (frame.width < 2 || frame.width > 2046 || frame.height < 2 || frame.height > 2047 || (frame.width & 1) != 0) {
      return "Frame dimensions are outside the GPU overlay limits";
    }
    if (frame.fullRange || frame.matrix == VideoColorMatrix::unsupported || frame.interlaced) {
      return "Source color range, matrix or interlacing requires software conversion";
    }
    for (std::size_t plane = 0; plane < frame.planes.size(); ++plane) {
      const int width = plane == 0 ? frame.width : frame.width / 2;
      const int height = plane == 0 ? frame.height : (frame.height + 1) / 2;
      if (frame.planes[plane] == nullptr || frame.pitches[plane] < width || frame.sizes[plane] < static_cast<std::uint64_t>(height - 1) * static_cast<unsigned int>(frame.pitches[plane]) + static_cast<unsigned int>(width)) {
        return "Source plane storage is invalid";
      }
    }
    return {};
  }

  bool pack_overlay_yuy2(const Yuv420FrameView &frame, std::uint8_t *destination, std::size_t capacity, int pitch) {
    const auto *luma = frame.planes[0];
    const auto *blueChroma = frame.planes[1];
    const auto *redChroma = frame.planes[2];
    if (luma == nullptr || blueChroma == nullptr || redChroma == nullptr) {
      return false;
    }
    if (!overlay_frame_error(frame).empty() || destination == nullptr || pitch < frame.width * 2 || capacity < static_cast<std::uint64_t>(frame.height - 1) * static_cast<unsigned int>(pitch) + static_cast<unsigned int>(frame.width * 2)) {
      return false;
    }
    for (int row = 0; row < frame.height; ++row) {
      const auto *y = luma + static_cast<std::size_t>(row) * frame.pitches[0];
      const auto *u = blueChroma + static_cast<std::size_t>(row / 2) * frame.pitches[1];
      const auto *v = redChroma + static_cast<std::size_t>(row / 2) * frame.pitches[2];
      auto *output = destination + static_cast<std::size_t>(row) * pitch;
      for (int column = 0; column < frame.width; column += 2) {
        output[column * 2] = y[column];
        output[column * 2 + 1] = u[column / 2];
        output[column * 2 + 2] = y[column + 1];
        output[column * 2 + 3] = v[column / 2];
      }
    }
    return true;
  }

  void VideoAccelerationReport::record_fallback(const std::string &reason) {
    if (fallbackReason.empty()) {
      fallbackReason = reason;
    }
  }

  std::vector<std::string> VideoAccelerationReport::summary_lines() const {
    std::vector<std::string> lines {
      fallbackReason.empty() ? (gpuFrames == 0 && softwareFrames == 0 ? "No video frames were presented" : "GPU presentation: no software fallback") : "SOFTWARE FALLBACK USED",
      "GPU overlay: " + std::to_string(gpuFrames) + " frames | Software: " + std::to_string(softwareFrames) + " frames",
    };
    if (!fallbackReason.empty()) {
      lines.push_back("Reason: " + fallbackReason);
    }
    lines.push_back("FFmpeg: " + cpuFeatures + (threaded ? " | Threading enabled" : " | THREADING DISABLED"));
    lines.emplace_back("Video decoding: CPU (GPU accelerates presentation)");
    return lines;
  }
}  // namespace streaming
