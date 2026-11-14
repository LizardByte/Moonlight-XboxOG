/**
 * @file src/streaming/video_acceleration.h
 * @brief Describes video planes, overlay packing, and persistent acceleration diagnostics.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace streaming {
  /**
   * @brief Color matrices understood by the hardware presentation path.
   */
  enum class VideoColorMatrix {
    bt601,  ///< Limited-range standard-definition YUV.
    bt709,  ///< Limited-range high-definition YUV.
    unsupported,  ///< Requires software conversion.
  };

  /**
   * @brief Bounds-checked view of a progressive planar YUV420 frame.
   */
  struct Yuv420FrameView {
    int width = 0;  ///< Luma width in pixels.
    int height = 0;  ///< Luma height in pixels.
    std::array<const std::uint8_t *, 3> planes {};  ///< Y, U and V storage.
    std::array<std::size_t, 3> sizes {};  ///< Accessible bytes in each plane.
    std::array<int, 3> pitches {};  ///< Positive byte strides.
    VideoColorMatrix matrix = VideoColorMatrix::bt601;  ///< Source conversion matrix.
    bool fullRange = false;  ///< Full-range YUV requires software conversion.
    bool interlaced = false;  ///< Interlaced sources require software presentation.
  };

  /**
   * @brief Rectangle in output framebuffer pixels.
   */
  struct VideoRectangle {
    int x = 0;  ///< Left edge.
    int y = 0;  ///< Top edge.
    int width = 0;  ///< Pixel width.
    int height = 0;  ///< Pixel height.
  };

  /**
   * @brief Validate source bounds and hardware-supported source properties.
   *
   * @param frame Source plane view.
   *
   * @return Empty on success, or an actionable software-fallback reason.
   */
  std::string overlay_frame_error(const Yuv420FrameView &frame);

  /**
   * @brief Pack YUV420 into Y0 U Y1 V bytes without color conversion or scaling.
   *
   * Missing planes or invalid source and destination bounds are rejected before
   * writing to the output buffer.
   *
   * @param frame Source planes to validate and pack.
   * @param destination Output storage.
   * @param capacity Accessible destination bytes.
   * @param pitch Destination byte stride.
   *
   * @return True when all source and destination accesses fit their buffers.
   */
  bool pack_overlay_yuy2(const Yuv420FrameView &frame, std::uint8_t *destination, std::size_t capacity, int pitch);

  /**
   * @brief Accumulates the paths used over an entire stream, including recovered fallbacks.
   */
  struct VideoAccelerationReport {
    std::uint64_t gpuFrames = 0;  ///< New frames submitted to the GPU overlay.
    std::uint64_t softwareFrames = 0;  ///< New frames presented through software conversion.
    std::string fallbackReason;  ///< First fallback reason, retained after later successes.
    std::string cpuFeatures;  ///< CPU optimizations enabled in the linked FFmpeg build.
    bool threaded = false;  ///< FFmpeg has internal synchronization, independently of the decoder worker count.

    /**
     * @brief Retain the first reason for using software presentation.
     *
     * @param reason Explanation supplied by the failed accelerated path.
     */
    void record_fallback(const std::string &reason);

    /**
     * @brief Build conspicuous end-of-stream diagnostics.
     * @return Text rows suitable for the stream summary and logs.
     */
    std::vector<std::string> summary_lines() const;
  };
}  // namespace streaming
