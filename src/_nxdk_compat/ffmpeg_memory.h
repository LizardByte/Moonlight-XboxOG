/**
 * @file src/_nxdk_compat/ffmpeg_memory.h
 * @brief Declares the aligned allocation family used privately by FFmpeg.
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

  /**
   * @brief Allocate memory with a power-of-two alignment.
   *
   * @param size Requested byte count.
   * @param alignment Alignment, at least the alignment of a pointer.
   *
   * @return Aligned allocation, or NULL with errno set on failure.
   */
  void *moonlight_ffmpeg_aligned_malloc(size_t size, size_t alignment);

  /**
   * @brief Resize an aligned allocation while preserving its contents on success.
   *
   * @param pointer Previous allocation, or NULL.
   * @param size New byte count; zero releases the allocation.
   * @param alignment Requested power-of-two alignment.
   *
   * @return Replacement allocation, or NULL; failure preserves the old allocation.
   */
  void *moonlight_ffmpeg_aligned_realloc(void *pointer, size_t size, size_t alignment);

  /**
   * @brief Release memory allocated by the private aligned allocation family.
   *
   * @param pointer Allocation to release, or NULL.
   */
  void moonlight_ffmpeg_aligned_free(void *pointer);

#ifdef __cplusplus
}
#endif
