/**
 * @file src/_nxdk_compat/ffmpeg_memory.c
 * @brief Implements aligned FFmpeg buffers without nxdk's incomplete CRT allocator.
 */
#include "ffmpeg_memory.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Store the owning allocation and logical size immediately before aligned data.
 */
typedef struct MoonlightAlignedAllocation {
  void *allocation;  ///< Pointer returned by malloc.
  size_t size;  ///< Requested byte count, used to bound reallocation copies.
} MoonlightAlignedAllocation;

void *moonlight_ffmpeg_aligned_malloc(size_t size, size_t alignment) {
  if (alignment < sizeof(void *) || (alignment & (alignment - 1)) != 0) {
    errno = EINVAL;
    return NULL;
  }
  const size_t overhead = sizeof(MoonlightAlignedAllocation) + alignment - 1;
  if (overhead < alignment || size > SIZE_MAX - overhead) {
    errno = ENOMEM;
    return NULL;
  }
  void *allocation = malloc(size + overhead);
  if (allocation == NULL) {
    errno = ENOMEM;
    return NULL;
  }
  const uintptr_t address = ((uintptr_t) allocation + sizeof(MoonlightAlignedAllocation) + alignment - 1) & ~(uintptr_t) (alignment - 1);
  MoonlightAlignedAllocation *header = (MoonlightAlignedAllocation *) address - 1;
  header->allocation = allocation;
  header->size = size;
  return (void *) address;
}

void moonlight_ffmpeg_aligned_free(void *pointer) {
  if (pointer != NULL) {
    free(((MoonlightAlignedAllocation *) pointer - 1)->allocation);
  }
}

void *moonlight_ffmpeg_aligned_realloc(void *pointer, size_t size, size_t alignment) {
  if (size == 0) {
    moonlight_ffmpeg_aligned_free(pointer);
    return NULL;
  }
  void *replacement = moonlight_ffmpeg_aligned_malloc(size, alignment);
  if (replacement != NULL && pointer != NULL) {
    const size_t previousSize = ((MoonlightAlignedAllocation *) pointer - 1)->size;
    memcpy(replacement, pointer, previousSize < size ? previousSize : size);
    moonlight_ffmpeg_aligned_free(pointer);
  }
  return replacement;
}
