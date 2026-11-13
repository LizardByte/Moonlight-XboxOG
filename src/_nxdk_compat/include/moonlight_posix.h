/**
 * @file src/_nxdk_compat/include/moonlight_posix.h
 * @brief Declare the small POSIX subset actually used by Moonlight on Xbox.
 */
#pragma once
#include <errno.h>
#ifndef EHOSTDOWN
  /** @brief Treat unavailable host-down errors as lwIP host-unreachable errors. */
  #define EHOSTDOWN EHOSTUNREACH
#endif
#include <sys/time.h>
#ifdef __cplusplus
extern "C" {
#endif
  /**
   * @brief Split a string using caller-owned iteration state.
   *
   * @param string Initial mutable string, or NULL to continue.
   * @param delimiters Characters separating tokens.
   * @param context Caller-owned continuation pointer.
   * @return Next token, or NULL when exhausted.
   */
  char *strtok_r(char *string, const char *delimiters, char **context);
  /**
   * @brief Sleep for at least the requested number of microseconds.
   *
   * @param microseconds Requested duration, rounded up to milliseconds.
   * @return Zero after the sleep completes.
   */
  int usleep(unsigned int microseconds);
#ifdef __cplusplus
}
#endif
