/**
 * @file src/streaming/video_overlay.cpp
 * @brief Converts a cached statistics bitmap for GPU scanout and software framebuffers.
 */
#include "src/streaming/video_overlay.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace {
  /**
   * @brief Validate destination storage before any panel copy.
   * @param pixels Destination pointer.
   * @param capacity Accessible bytes.
   * @param pitch Byte stride.
   * @param width Pixel width.
   * @param height Pixel height.
   * @param bytes Bytes per pixel.
   * @return True when every row fits its storage.
   */
  bool valid_storage(std::uint8_t *pixels, std::size_t capacity, int pitch, int width, int height, int bytes) {
    return pixels != nullptr && width > 0 && height > 0 && bytes > 0 && pitch > 0 && static_cast<std::int64_t>(pitch) >= static_cast<std::int64_t>(width) * bytes &&
           capacity >= static_cast<std::uint64_t>(height - 1) * pitch + static_cast<std::uint64_t>(width) * bytes;
  }
}  // namespace

namespace streaming {
  void VideoOverlay::set_bitmap(int width, int height, std::vector<std::uint8_t> pixels) {
    if (width <= 0 || height <= 0 || width > 2048 || height > 2048 || pixels.size() != static_cast<std::size_t>(width) * height) {
      width = height = 0;
      pixels.clear();
    }
    width_ = width;
    height_ = height;
    grayscale_ = std::move(pixels);
    rgb_.clear();
    rgbBpp_ = 0;
    yuy2_.clear();
    frameWidth_ = 0;
  }

  int VideoOverlay::width() const {
    return width_;
  }

  int VideoOverlay::height() const {
    return height_;
  }

  void VideoOverlay::prepare_rgb(int bpp) {
    if (rgbBpp_ == bpp) {
      return;
    }
    const int bytes = bpp == 32 ? 4 : 2;
    rgb_.resize(grayscale_.size() * bytes);
    for (std::size_t i = 0; i < grayscale_.size(); ++i) {
      const std::uint32_t gray = grayscale_[i];
      if (bpp == 32) {
        const std::uint32_t color = 0xff000000U | gray * 0x010101U;
        std::memcpy(rgb_.data() + i * 4, &color, 4);
      } else {
        const auto color = static_cast<std::uint16_t>(bpp == 16 ? ((gray >> 3U) << 11U) | ((gray >> 2U) << 5U) | (gray >> 3U) : 0x8000U | ((gray >> 3U) << 10U) | ((gray >> 3U) << 5U) | (gray >> 3U));
        std::memcpy(rgb_.data() + i * 2, &color, 2);
      }
    }
    rgbBpp_ = bpp;
  }

  const std::vector<std::uint8_t> &VideoOverlay::argb_pixels() {
    prepare_rgb(32);
    return rgb_;
  }

  bool VideoOverlay::draw_rgb(std::uint8_t *pixels, std::size_t capacity, int pitch, int width, int height, int bpp, const VideoRectangle &destination) {
    if (grayscale_.empty()) {
      return true;
    }
    const int bytes = bpp == 32 ? 4 : (bpp == 15 || bpp == 16 ? 2 : 0);
    if (!valid_storage(pixels, capacity, pitch, width, height, bytes) || destination.x < 0 || destination.y < 0 || destination.width <= MARGIN || destination.height <= MARGIN || destination.x > width - destination.width || destination.y > height - destination.height) {
      return false;
    }
    prepare_rgb(bpp);
    const int copyWidth = std::min(width_, destination.width - MARGIN);
    const int copyHeight = std::min(height_, destination.height - MARGIN);
    for (int row = 0; row < copyHeight; ++row) {
      std::memcpy(pixels + static_cast<std::size_t>(destination.y + MARGIN + row) * pitch + (destination.x + MARGIN) * bytes, rgb_.data() + static_cast<std::size_t>(row) * width_ * bytes, copyWidth * bytes);
    }
    return true;
  }

  bool VideoOverlay::draw_yuy2(std::uint8_t *pixels, std::size_t capacity, int pitch, int frameWidth, int frameHeight, const VideoRectangle &destination) {
    if (grayscale_.empty()) {
      return true;
    }
    if (!valid_storage(pixels, capacity, pitch, frameWidth, frameHeight, 2) || (frameWidth & 1) != 0 || frameWidth > 2046 || frameHeight > 2047 || destination.width <= MARGIN || destination.height <= MARGIN) {
      return false;
    }
    if (frameWidth_ != frameWidth || frameHeight_ != frameHeight || outputWidth_ != destination.width || outputHeight_ != destination.height) {
      frameWidth_ = frameWidth;
      frameHeight_ = frameHeight;
      outputWidth_ = destination.width;
      outputHeight_ = destination.height;
      source_.x = ((MARGIN * frameWidth / destination.width) + 1) & ~1;
      source_.y = MARGIN * frameHeight / destination.height;
      const int visibleWidth = std::min(width_, destination.width - MARGIN);
      const int visibleHeight = std::min(height_, destination.height - MARGIN);
      source_.width = std::min(frameWidth - source_.x, ((visibleWidth * frameWidth + destination.width - 1) / destination.width + 1) & ~1);
      source_.height = std::min(frameHeight - source_.y, (visibleHeight * frameHeight + destination.height - 1) / destination.height);
      if (source_.width <= 0 || source_.height <= 0) {
        return true;
      }
      yuy2_.resize(static_cast<std::size_t>(source_.width) * source_.height * 2);
      for (int row = 0; row < source_.height; ++row) {
        const int bitmapRow = std::min(visibleHeight - 1, row * visibleHeight / source_.height);
        for (int column = 0; column < source_.width; ++column) {
          const int bitmapColumn = std::min(visibleWidth - 1, column * visibleWidth / source_.width);
          const auto gray = grayscale_[bitmapRow * width_ + bitmapColumn];
          const std::size_t offset = (static_cast<std::size_t>(row) * source_.width + column) * 2;
          yuy2_[offset] = static_cast<std::uint8_t>(16 + (gray * 219 + 127) / 255);
          yuy2_[offset + 1] = 128;
        }
      }
    }
    if (source_.width <= 0 || source_.height <= 0) {
      return true;
    }
    for (int row = 0; row < source_.height; ++row) {
      std::memcpy(pixels + static_cast<std::size_t>(source_.y + row) * pitch + source_.x * 2, yuy2_.data() + static_cast<std::size_t>(row) * source_.width * 2, source_.width * 2);
    }
    return true;
  }
}  // namespace streaming
