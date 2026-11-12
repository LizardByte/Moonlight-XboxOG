/**
 * @file src/_nxdk_compat/fenv_compat.h
 * @brief Provides x87 rounding control for nxdk floating-point environment adapters.
 */
#pragma once

#include <cstdint>

#if defined(__i386__) || defined(__x86_64__)
namespace nxdk_compat {

  /**
   * @brief Change x87 rounding without changing exception masks or precision control.
   *
   * @param roundingMode Rounding mode encoded in the x87 control word bits 10 and 11.
   * @return Zero on success, or -1 for an invalid rounding mode.
   */
  inline int set_x87_rounding_mode(int roundingMode) {
    constexpr int ROUNDING_MODE_MASK = 0x0C00;  ///< Rounding control bits in the x87 control word.
    if ((roundingMode & ~ROUNDING_MODE_MASK) != 0) {
      return -1;
    }

    std::uint16_t controlWord = 0;
    asm volatile("fnstcw %0" : "=m"(controlWord));
    controlWord = static_cast<std::uint16_t>((controlWord & ~ROUNDING_MODE_MASK) | roundingMode);
    asm volatile("fldcw %0" : : "m"(controlWord));
    return 0;
  }

}  // namespace nxdk_compat
#endif
