/**
 * @file src/_nxdk_compat/share.h
 * @brief Provides the small share.h surface needed by the FFmpeg Xbox build.
 */

#pragma once

#include <errno.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SH_DENYNO
  #define SH_DENYNO 0
#endif

  /**
   * @brief Stub the wide-character shared open helper used by FFmpeg's Win32 path.
   *
   * @param path Requested file system path.
   * @param oflag Requested open flags.
   * @param shflag Requested sharing mode.
   * @param pmode Requested permission mode.
   * @return Always -1 with errno set to ENOSYS.
   */
  static inline int _wsopen(const wchar_t *path, int oflag, int shflag, int pmode) {
    (void) path;
    (void) oflag;
    (void) shflag;
    (void) pmode;
    errno = ENOSYS;
    return -1;
  }

  /**
   * @brief Stub the narrow shared open helper used by FFmpeg's Win32 path.
   *
   * @param path Requested file system path.
   * @param oflag Requested open flags.
   * @param shflag Requested sharing mode.
   * @param pmode Requested permission mode.
   * @return Always -1 with errno set to ENOSYS.
   */
  static inline int _sopen(const char *path, int oflag, int shflag, int pmode) {
    (void) path;
    (void) oflag;
    (void) shflag;
    (void) pmode;
    errno = ENOSYS;
    return -1;
  }

#ifdef __cplusplus
}
#endif
