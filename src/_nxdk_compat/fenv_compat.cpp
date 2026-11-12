/**
 * @file src/_nxdk_compat/fenv_compat.cpp
 * @brief Implements the C++ floating-point environment functions declared by nxdk.
 */
#ifdef NXDK

  #include "src/_nxdk_compat/fenv_compat.h"

  #include <cfenv>

/**
 * @brief Save the x87 environment, mask exceptions, and clear pending exception flags.
 *
 * @param environment Destination for the saved nxdk floating-point environment.
 * @return Zero on success.
 */
int feholdexcept(fenv_t *environment) {
  asm volatile("fnstenv %0\n\tfnclex" : "=m"(*environment));
  return 0;
}

/**
 * @brief Restore the x87 environment saved by feholdexcept().
 *
 * @param environment Saved nxdk floating-point environment to restore.
 * @return Zero on success.
 */
int fesetenv(const fenv_t *environment) {
  asm volatile("fldenv %0" : : "m"(*environment));
  return 0;
}

/**
 * @brief Change x87 rounding while preserving exception masks and precision control.
 *
 * @param roundingMode One of the FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, or FE_TOWARDZERO modes.
 * @return Zero on success, or -1 for an invalid rounding mode.
 */
int fesetround(int roundingMode) {
  return nxdk_compat::set_x87_rounding_mode(roundingMode);
}

#endif
