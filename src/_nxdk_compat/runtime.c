/**
 * @file src/_nxdk_compat/runtime.c
 * @brief Implement Moonlight's timing and reentrant string helpers on stock nxdk.
 */
#include "include/moonlight_posix.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <windows.h>
#include <xboxkrnl/xboxkrnl.h>

int gettimeofday(struct timeval *value, void *timezone) {
  if (value == NULL || timezone != NULL) {
    errno = EINVAL;
    return -1;
  }
  LARGE_INTEGER now;
  KeQuerySystemTime(&now);
  const int64_t ticks = now.QuadPart - INT64_C(116444736000000000);
  int64_t seconds = ticks / INT64_C(10000000);
  int64_t remainder = ticks % INT64_C(10000000);
  if (remainder < 0) {
    --seconds;
    remainder += INT64_C(10000000);
  }
  if (seconds < LONG_MIN || seconds > LONG_MAX) {
    errno = EOVERFLOW;
    return -1;
  }
  value->tv_sec = (long) seconds;
  value->tv_usec = (long) (remainder / 10);
  return 0;
}

char *strtok_r(char *string, const char *delimiters, char **context) {
  char *token = string != NULL ? string : *context;
  if (token == NULL) {
    return NULL;
  }
  token += strspn(token, delimiters);
  if (*token == '\0') {
    *context = token;
    return NULL;
  }
  char *end = token + strcspn(token, delimiters);
  if (*end != '\0') {
    *end++ = '\0';
  }
  *context = end;
  return token;
}

int usleep(unsigned int microseconds) {
  Sleep(microseconds / 1000U + (microseconds % 1000U != 0));
  return 0;
}
