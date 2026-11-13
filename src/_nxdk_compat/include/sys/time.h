/**
 * @file src/_nxdk_compat/include/sys/time.h
 * @brief Share the real Xbox wall clock with dependency and lwIP code.
 */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
  /** @brief Unix timestamp with microsecond resolution. */
  struct timeval {
    long tv_sec;  ///< Seconds since the Unix epoch.
    long tv_usec;  ///< Microseconds within the second.
  };

  /**
   * @brief Read the Xbox wall clock as a Unix timestamp.
   *
   * @param value Timestamp destination.
   * @param timezone Unused legacy timezone argument; must be NULL.
   * @return Zero on success or -1 with errno on invalid arguments.
   */
  int gettimeofday(struct timeval *value, void *timezone);
#ifdef __cplusplus
}
#endif
