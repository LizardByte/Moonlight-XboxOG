/**
 * @file tests/support/pvideo/pbkit/pbkit.h
 * @brief Declares the pbkit calls used by the presenter lifecycle fixture.
 */
#pragma once

#include <cstdint>

inline constexpr unsigned int NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8 = 8;  ///< 32-bit scanout format.

/**
 * @brief Read simulated vertical blanks.
 *
 * @return Counter advanced by Sleep.
 */
std::uint32_t pb_get_vbl_counter();
/**
 * @brief Query fake command completion.
 *
 * @return Zero when idle.
 */
int pb_busy();
/**
 * @brief Initialize fake GPU resources.
 *
 * @return Configured pbkit status.
 */
int pb_init();
/**
 * @brief Release fake GPU resources, disabling video like nxdk.
 */
void pb_kill();
/**
 * @brief Select a color format.
 *
 * @param format GPU format.
 * @param swizzled Texture layout.
 */
void pb_set_color_format(unsigned int format, bool swizzled);
/**
 * @brief Clear the GPU background.
 *
 * @param x Left.
 * @param y Top.
 * @param width Width.
 * @param height Height.
 * @param color Packed color.
 */
void pb_fill(int x, int y, int width, int height, std::uint32_t color);
/**
 * @brief Get the separate GPU scanout storage.
 *
 * @return Back buffer.
 */
std::uint32_t *pb_back_buffer();
/**
 * @brief Get GPU row stride.
 *
 * @return Byte pitch.
 */
unsigned int pb_back_buffer_pitch();
