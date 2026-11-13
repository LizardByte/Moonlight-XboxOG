/**
 * @file src/_nxdk_compat/openssl_compat.h
 * @brief Adapt OpenSSL's single-process assumptions to the Xbox runtime.
 *
 * POSIX descriptor I/O is disabled by Configure; pairing uses a custom socket BIO.
 */
#pragma once

/** @brief Request Annex K time conversion declarations from PDCLib. */
#define __STDC_WANT_LIB_EXT1__ 1
#include <time.h>

/**
 * @brief Identify the sole Xbox application for OpenSSL's fork detection.
 * @return Stable process identifier; Xbox applications cannot fork.
 */
static inline int moonlight_openssl_getpid(void) {
  return 1;
}

/**
 * @brief Identify the single privileged Xbox security identity.
 * @return Zero; distinct real and effective users or groups do not exist.
 */
static inline unsigned int moonlight_openssl_identity(void) {
  return 0;
}

/**
 * @brief Adapt PDCLib's Annex K gmtime_s argument order for OpenSSL.
 * @param result Destination broken-down timestamp.
 * @param timer Source Unix timestamp.
 * @return Zero on success, or the PDCLib error code.
 */
static inline int moonlight_openssl_gmtime_s(struct tm *result, const time_t *timer) {
  return gmtime_s(timer, result);
}

/** @brief Describe the sole application to OpenSSL's fork detection. */
#define getpid moonlight_openssl_getpid
/** @brief Describe the real user identity to OpenSSL. */
#define getuid moonlight_openssl_identity
/** @brief Describe the effective user identity to OpenSSL. */
#define geteuid moonlight_openssl_identity
/** @brief Describe the real group identity to OpenSSL. */
#define getgid moonlight_openssl_identity
/** @brief Describe the effective group identity to OpenSSL. */
#define getegid moonlight_openssl_identity
/** @brief Use PDCLib's Annex K time conversion. */
#define gmtime_s moonlight_openssl_gmtime_s
