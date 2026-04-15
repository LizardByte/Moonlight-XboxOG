/**
 * @file src/_nxdk_compat/openssl_compat.h
 * @brief Declares OpenSSL compatibility shims for nxdk.
 */
#pragma once

#ifndef __STDC_WANT_LIB_EXT1__
  /**
   * @brief Request Annex K declarations such as gmtime_s when the C library provides them.
   */
  #define __STDC_WANT_LIB_EXT1__ 1
#endif

#include <lwip/opt.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

  /**
   * @brief Receive bytes through the lwIP socket backend.
   *
   * @param s lwIP socket descriptor.
   * @param mem Destination buffer.
   * @param len Maximum number of bytes to receive.
   * @param flags lwIP receive flags.
   * @return Number of bytes received, or a negative value on failure.
   */
  ssize_t lwip_recv(int s, void *mem, size_t len, int flags);

  /**
   * @brief Send bytes through the lwIP socket backend.
   *
   * @param s lwIP socket descriptor.
   * @param dataptr Source buffer.
   * @param size Number of bytes to send.
   * @param flags lwIP send flags.
   * @return Number of bytes sent, or a negative value on failure.
   */
  ssize_t lwip_send(int s, const void *dataptr, size_t size, int flags);

  /**
   * @brief Wait for lwIP socket readiness using the nxdk select implementation.
   *
   * @param maxfdp1 One greater than the highest descriptor to inspect.
   * @param readset Optional descriptor set watched for readability.
   * @param writeset Optional descriptor set watched for writability.
   * @param exceptset Optional descriptor set watched for exceptional conditions.
   * @param timeout Optional timeout value.
   * @return Number of ready descriptors, zero on timeout, or a negative value on failure.
   */
  int lwip_select(int maxfdp1, struct fd_set *readset, struct fd_set *writeset, struct fd_set *exceptset, struct timeval *timeout);

#ifndef LWIP_SOCKET_OFFSET
  /**
   * @brief Offset applied by lwIP when translating socket descriptors.
   */
  #define LWIP_SOCKET_OFFSET 0
#endif

#ifndef FD_SETSIZE
  /**
   * @brief Maximum number of sockets tracked by the compatibility fd_set.
   */
  #define FD_SETSIZE MEMP_NUM_NETCONN
#endif

#ifndef FD_SET
  /**
   * @brief Minimal socket descriptor set used by the lwIP-backed select shim.
   */
  typedef struct fd_set {
    unsigned char fd_bits[(FD_SETSIZE + 7) / 8];  ///< Bitset storing tracked socket descriptors relative to LWIP_SOCKET_OFFSET.
  } fd_set;

  /**
   * @brief Mark a socket descriptor as present in an fd_set.
   */
  #define FD_SET(n, p) ((p)->fd_bits[((n) - LWIP_SOCKET_OFFSET) / 8] = (unsigned char) ((p)->fd_bits[((n) - LWIP_SOCKET_OFFSET) / 8] | (1u << (((n) - LWIP_SOCKET_OFFSET) & 7))))

  /**
   * @brief Clear a socket descriptor from an fd_set.
   */
  #define FD_CLR(n, p) ((p)->fd_bits[((n) - LWIP_SOCKET_OFFSET) / 8] = (unsigned char) ((p)->fd_bits[((n) - LWIP_SOCKET_OFFSET) / 8] & ~(1u << (((n) - LWIP_SOCKET_OFFSET) & 7))))

  /**
   * @brief Return whether a socket descriptor is present in an fd_set.
   */
  #define FD_ISSET(n, p) (((p)->fd_bits[((n) - LWIP_SOCKET_OFFSET) / 8] & (1u << (((n) - LWIP_SOCKET_OFFSET) & 7))) != 0)

  /**
   * @brief Reset an fd_set so that it tracks no descriptors.
   */
  #define FD_ZERO(p) memset((void *) (p), 0, sizeof(*(p)))
#endif

#ifndef select
  /**
   * @brief Route select calls through the lwIP compatibility shim on nxdk builds.
   */
  #define select(maxfdp1, readset, writeset, exceptset, timeout) lwip_select(maxfdp1, readset, writeset, exceptset, timeout)
#endif

#ifndef F_OK
  /**
   * @brief Test access mode flag for file existence checks.
   */
  #define F_OK 0
#endif

#ifndef R_OK
  /**
   * @brief Test access mode flag for read permission checks.
   */
  #define R_OK 4
#endif

#ifndef W_OK
  /**
   * @brief Test access mode flag for write permission checks.
   */
  #define W_OK 2
#endif

#ifndef X_OK
  /**
   * @brief Test access mode flag for execute permission checks.
   */
  #define X_OK 1
#endif

#ifndef AF_UNIX
  /**
   * @brief Placeholder address-family value used when AF_UNIX is unavailable on nxdk.
   */
  #define AF_UNIX (-1)
#endif

/** @brief Redirect access to the nxdk OpenSSL compatibility shim. */
#define access moonlight_nxdk_openssl_access
/** @brief Redirect fileno to the nxdk OpenSSL compatibility shim. */
#define fileno moonlight_nxdk_openssl_fileno
/** @brief Redirect read to the nxdk OpenSSL compatibility shim. */
#define read moonlight_nxdk_openssl_read
/** @brief Redirect write to the nxdk OpenSSL compatibility shim. */
#define write moonlight_nxdk_openssl_write
/** @brief Redirect close to the nxdk OpenSSL compatibility shim. */
#define close moonlight_nxdk_openssl_close
/** @brief Redirect _close to the nxdk OpenSSL compatibility shim. */
#define _close moonlight_nxdk_openssl__close
/** @brief Redirect open to the nxdk OpenSSL compatibility shim. */
#define open moonlight_nxdk_openssl_open
/** @brief Redirect _open to the nxdk OpenSSL compatibility shim. */
#define _open moonlight_nxdk_openssl__open
/** @brief Redirect fdopen to the nxdk OpenSSL compatibility shim. */
#define fdopen moonlight_nxdk_openssl_fdopen
/** @brief Redirect _fdopen to the nxdk OpenSSL compatibility shim. */
#define _fdopen moonlight_nxdk_openssl__fdopen
/** @brief Redirect _unlink to the nxdk OpenSSL compatibility shim. */
#define _unlink moonlight_nxdk_openssl__unlink
/** @brief Redirect chmod to the nxdk OpenSSL compatibility shim. */
#define chmod moonlight_nxdk_openssl_chmod
/** @brief Redirect getuid to the nxdk OpenSSL compatibility shim. */
#define getuid moonlight_nxdk_openssl_getuid
/** @brief Redirect geteuid to the nxdk OpenSSL compatibility shim. */
#define geteuid moonlight_nxdk_openssl_geteuid
/** @brief Redirect getgid to the nxdk OpenSSL compatibility shim. */
#define getgid moonlight_nxdk_openssl_getgid
/** @brief Redirect getegid to the nxdk OpenSSL compatibility shim. */
#define getegid moonlight_nxdk_openssl_getegid

  /**
   * @brief Stub access for file-system queries that OpenSSL may issue on nxdk.
   */
  static inline int moonlight_nxdk_openssl_access(const char *path, int mode) {
    (void) path;
    (void) mode;
    return -1;
  }

  /**
   * @brief Stub fileno for stdio streams that do not expose POSIX descriptors on nxdk.
   */
  static inline int moonlight_nxdk_openssl_fileno(FILE *stream) {
    (void) stream;
    return -1;
  }

  /**
   * @brief Forward OpenSSL read calls to lwIP recv.
   */
  static inline ssize_t moonlight_nxdk_openssl_read(int fd, void *buffer, size_t count) {
    return lwip_recv(fd, buffer, count, 0);
  }

  /**
   * @brief Forward OpenSSL write calls to lwIP send.
   */
  static inline ssize_t moonlight_nxdk_openssl_write(int fd, const void *buffer, size_t count) {
    return lwip_send(fd, buffer, count, 0);
  }

  /**
   * @brief Stub close for descriptors that are not backed by a host file system on nxdk.
   */
  static inline int moonlight_nxdk_openssl_close(int fd) {
    (void) fd;
    return -1;
  }

  /**
   * @brief Windows-style alias for the close compatibility shim.
   */
  static inline int moonlight_nxdk_openssl__close(int fd) {
    return moonlight_nxdk_openssl_close(fd);
  }

  /**
   * @brief Stub open for OpenSSL paths that are unsupported on nxdk.
   */
  static inline int moonlight_nxdk_openssl_open(const char *path, int flags, ...) {
    (void) path;
    (void) flags;
    return -1;
  }

  /**
   * @brief Windows-style alias for the open compatibility shim.
   */
  static inline int moonlight_nxdk_openssl__open(const char *path, int flags, ...) {
    (void) path;
    (void) flags;
    return -1;
  }

  /**
   * @brief Stub fdopen for descriptor-backed stdio that is unavailable on nxdk.
   */
  static inline FILE *moonlight_nxdk_openssl_fdopen(int fd, const char *mode) {
    (void) fd;
    (void) mode;
    return NULL;
  }

  /**
   * @brief Windows-style alias for the fdopen compatibility shim.
   */
  static inline FILE *moonlight_nxdk_openssl__fdopen(int fd, const char *mode) {
    return moonlight_nxdk_openssl_fdopen(fd, mode);
  }

  /**
   * @brief Stub unlink for OpenSSL cleanup paths that are unsupported on nxdk.
   */
  static inline int moonlight_nxdk_openssl__unlink(const char *path) {
    (void) path;
    return -1;
  }

  /**
   * @brief Stub chmod for OpenSSL paths that are unsupported on nxdk.
   */
  static inline int moonlight_nxdk_openssl_chmod(const char *path, int mode) {
    (void) path;
    (void) mode;
    return -1;
  }

  /**
   * @brief Return a placeholder user identifier for nxdk builds.
   */
  static inline unsigned int moonlight_nxdk_openssl_getuid(void) {
    return 0;
  }

  /**
   * @brief Return a placeholder effective user identifier for nxdk builds.
   */
  static inline unsigned int moonlight_nxdk_openssl_geteuid(void) {
    return 0;
  }

  /**
   * @brief Return a placeholder group identifier for nxdk builds.
   */
  static inline unsigned int moonlight_nxdk_openssl_getgid(void) {
    return 0;
  }

  /**
   * @brief Return a placeholder effective group identifier for nxdk builds.
   */
  static inline unsigned int moonlight_nxdk_openssl_getegid(void) {
    return 0;
  }

  /**
   * @brief Adapt Microsoft's gmtime_s parameter order to the Annex K signature expected by OpenSSL.
   */
  static inline int moonlight_nxdk_openssl_gmtime_s(struct tm *result, const time_t *timer) {
    return gmtime_s(timer, result);
  }

/** @brief Redirect gmtime_s to the nxdk OpenSSL compatibility shim. */
#define gmtime_s moonlight_nxdk_openssl_gmtime_s

#ifdef __cplusplus
}
#endif
