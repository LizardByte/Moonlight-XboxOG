/**
 * @file src/streaming/video_overlay.h
 * @brief Caches one grayscale statistics panel for both YUY2 and RGB presentation.
 */
#pragma once

#include "src/streaming/video_acceleration.h"

namespace streaming {
  /**
   * @brief Opaque grayscale panel anchored inside the top-left of the video rectangle.
   */
  class VideoOverlay {
  public:
    /**
     * @brief Replace the panel and invalidate its converted caches.
     * @param width Bitmap width.
     * @param height Bitmap height.
     * @param pixels Packed grayscale bytes; invalid storage clears the panel.
     */
    void set_bitmap(int width, int height, std::vector<std::uint8_t> pixels);
    /**
     * @brief Get the panel width.
     *
     * @return Width, or zero for a cleared panel.
     */
    int width() const;
    /**
     * @brief Get the panel height.
     *
     * @return Height, or zero for a cleared panel.
     */
    int height() const;
    /**
     * @brief Get cached opaque ARGB pixels for SDL.
     *
     * @return Packed pixels.
     */
    const std::vector<std::uint8_t> &argb_pixels();
    /**
     * @brief Composite the panel into a validated packed YUY2 frame before GPU submission.
     * @param pixels Destination frame.
     * @param capacity Available bytes.
     * @param pitch Row stride in bytes.
     * @param frameWidth Frame width.
     * @param frameHeight Frame height.
     * @param destination Output video rectangle controlling panel scale.
     * @return True when the panel was copied or is empty.
     */
    bool draw_yuy2(std::uint8_t *pixels, std::size_t capacity, int pitch, int frameWidth, int frameHeight, const VideoRectangle &destination);
    /**
     * @brief Composite the same panel into the final software framebuffer.
     * @param pixels Destination storage.
     * @param capacity Available bytes.
     * @param pitch Row stride in bytes.
     * @param width Framebuffer width.
     * @param height Framebuffer height.
     * @param bpp Xbox RGB pixel depth: 15, 16 or 32.
     * @param destination Output video rectangle.
     * @return True when drawing completed or the panel is empty.
     */
    bool draw_rgb(std::uint8_t *pixels, std::size_t capacity, int pitch, int width, int height, int bpp, const VideoRectangle &destination);

    static constexpr int MARGIN = 8;  ///< Panel inset in output pixels.

  private:
    /**
     * @brief Convert the grayscale bitmap only when its pixel depth changes.
     *
     * @param bpp RGB pixel depth.
     */
    void prepare_rgb(int bpp);
    int width_ = 0;  ///< Packed panel width.
    int height_ = 0;  ///< Packed panel height.
    std::vector<std::uint8_t> grayscale_;  ///< Font raster and opaque background.
    std::vector<std::uint8_t> rgb_;  ///< Cached RGB panel.
    int rgbBpp_ = 0;  ///< Pixel format of the RGB cache.
    std::vector<std::uint8_t> yuy2_;  ///< Cached source-resolution panel for scanout scaling.
    VideoRectangle source_ {};  ///< Panel bounds in source luma pixels.
    int frameWidth_ = 0;  ///< Source width used by the YUY2 cache.
    int frameHeight_ = 0;  ///< Source height used by the YUY2 cache.
    int outputWidth_ = 0;  ///< Output width used by the YUY2 cache.
    int outputHeight_ = 0;  ///< Output height used by the YUY2 cache.
  };
}  // namespace streaming
