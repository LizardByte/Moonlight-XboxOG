/**
 * @file src/_nxdk_compat/nanors_compat.h
 * @brief Select the scalar nanors backend for the original Xbox CPU.
 *
 * The Xbox CPU lacks the SSSE3 and newer instructions used by nanors' x86
 * backends. Disable those backends after architecture detection to avoid
 * dependencies on CPU-dispatch runtime helpers unavailable in nxdk.
 */
#pragma once

#ifdef NXDK
  #include "oblas_common.h"

  #undef OBLAS_ARCH_X86
#endif
