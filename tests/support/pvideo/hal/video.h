/**
 * @file tests/support/pvideo/hal/video.h
 * @brief Declares the fake scanout registers used to exercise the real Xbox presenter on the host.
 */
#pragma once

#include <cstdint>

/**
 * @brief Active framebuffer dimensions and format.
 */
struct VIDEO_MODE {
  int width;  ///< Width in pixels.
  int height;  ///< Height in pixels.
  int bpp;  ///< Bits per pixel.
  int refresh;  ///< Refresh rate.
};

/**
 * @brief Access a fake 32-bit NV2A register.
 * @param address Register offset.
 * @return Mutable register storage.
 */
std::uint32_t &pvideo_test_register(std::uint32_t address);
/**
 * @brief Access an indexed CRTC register or its selector.
 * @param address Register offset.
 * @return Mutable byte register storage.
 */
std::uint8_t &pvideo_test_register8(std::uint32_t address);
#define VIDEOREG(address) pvideo_test_register(address)  ///< Redirect MMIO into the fixture.
#define VIDEOREG8(address) pvideo_test_register8(address)  ///< Redirect indexed CRTC MMIO into the fixture.

/**
 * @brief Get the fake active output mode.
 *
 * @return Framebuffer dimensions.
 */
VIDEO_MODE XVideoGetMode();
/**
 * @brief Get the CPU framebuffer.
 *
 * @return Mutable framebuffer storage.
 */
unsigned char *XVideoGetFB();
/**
 * @brief Enable or disable output.
 *
 * @param enable Nonzero enables video.
 */
void XVideoSetVideoEnable(int enable);
