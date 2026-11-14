/**
 * @file tests/support/pvideo/windows.h
 * @brief Supplies deterministic time and allocation flags for presenter lifecycle tests.
 */
#pragma once

#include <cstdint>

using DWORD = std::uint32_t;  ///< Xbox millisecond tick type.
inline constexpr int TRUE = 1;  ///< Enabled output.
inline constexpr int PAGE_READWRITE = 4;  ///< Writable allocation flag.
inline constexpr int PAGE_WRITECOMBINE = 0x400;  ///< Write-combined allocation flag.

/**
 * @brief Get simulated milliseconds.
 *
 * @return Fixture clock.
 */
DWORD GetTickCount();
/**
 * @brief Advance time and simulated vblank.
 *
 * @param milliseconds Simulated sleep duration.
 */
void Sleep(DWORD milliseconds);
