/**
 * @file tests/unit/streaming/ffmpeg_memory_test.cpp
 * @brief Checks aligned allocation ownership, preservation and failure semantics.
 */
// test includes
// test includes
#include <lizardbyte/common/testing.h>

// standard includes
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <limits>

// local includes
#include "src/_nxdk_compat/ffmpeg_memory.h"

/**
 * @brief Growing and shrinking preserve the relevant bytes and requested alignment.
 */
TEST(FfmpegMemoryTest, PreservesDataAcrossAlignedReallocation) {
  auto *memory = static_cast<std::uint8_t *>(moonlight_ffmpeg_aligned_malloc(73, 64));
  ASSERT_NE(memory, nullptr);
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(memory) % 64, 0U);
  std::memset(memory, 0x5a, 73);
  auto *grown = static_cast<std::uint8_t *>(moonlight_ffmpeg_aligned_realloc(memory, 1000, 64));
  ASSERT_NE(grown, nullptr);
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(grown) % 64, 0U);
  for (std::size_t index = 0; index < 73; ++index) {
    EXPECT_EQ(grown[index], 0x5a);
  }
  auto *shrunk = static_cast<std::uint8_t *>(moonlight_ffmpeg_aligned_realloc(grown, 17, 32));
  ASSERT_NE(shrunk, nullptr);
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(shrunk) % 32, 0U);
  for (std::size_t index = 0; index < 17; ++index) {
    EXPECT_EQ(shrunk[index], 0x5a);
  }
  moonlight_ffmpeg_aligned_free(shrunk);
}

/**
 * @brief Failed reallocations leave the original allocation usable and owned by the caller.
 */
TEST(FfmpegMemoryTest, FailureDoesNotLoseOriginalAllocation) {
  auto *memory = static_cast<std::uint8_t *>(moonlight_ffmpeg_aligned_malloc(16, 16));
  ASSERT_NE(memory, nullptr);
  memory[0] = 42;
  EXPECT_EQ(moonlight_ffmpeg_aligned_realloc(memory, std::numeric_limits<std::size_t>::max(), 16), nullptr);
  EXPECT_EQ(errno, ENOMEM);
  EXPECT_EQ(memory[0], 42);
  EXPECT_EQ(moonlight_ffmpeg_aligned_realloc(memory, 32, 3), nullptr);
  EXPECT_EQ(errno, EINVAL);
  EXPECT_EQ(memory[0], 42);
  EXPECT_EQ(moonlight_ffmpeg_aligned_realloc(memory, 0, 16), nullptr);
  moonlight_ffmpeg_aligned_free(nullptr);
}
