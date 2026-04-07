#pragma once

#ifndef __STDC_WANT_LIB_EXT1__
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

  ssize_t lwip_recv(int s, void *mem, size_t len, int flags);
  ssize_t lwip_send(int s, const void *dataptr, size_t size, int flags);
  int lwip_select(int maxfdp1, struct fd_set *readset, struct fd_set *writeset, struct fd_set *exceptset, struct timeval *timeout);

#ifndef LWIP_SOCKET_OFFSET
  #define LWIP_SOCKET_OFFSET 0
#endif

#ifndef FD_SETSIZE
  #define FD_SETSIZE MEMP_NUM_NETCONN
#endif

#ifndef FD_SET
  /**
   * @brief Minimal socket descriptor set used by the lwIP-backed select shim.
   */
  typedef struct fd_set {
    unsigned char fd_bits[(FD_SETSIZE + 7) / 8];  ///< Bitset storing tracked socket descriptors relative to LWIP_SOCKET_OFFSET.
  } fd_set;

  #define FD_SET(n, p) ((p)->fd_bits[((n) - LWIP_SOCKET_OFFSET) / 8] = (unsigned char) ((p)->fd_bits[((n) - LWIP_SOCKET_OFFSET) / 8] | (1u << (((n) - LWIP_SOCKET_OFFSET) & 7))))
  #define FD_CLR(n, p) ((p)->fd_bits[((n) - LWIP_SOCKET_OFFSET) / 8] = (unsigned char) ((p)->fd_bits[((n) - LWIP_SOCKET_OFFSET) / 8] & ~(1u << (((n) - LWIP_SOCKET_OFFSET) & 7))))
  #define FD_ISSET(n, p) (((p)->fd_bits[((n) - LWIP_SOCKET_OFFSET) / 8] & (1u << (((n) - LWIP_SOCKET_OFFSET) & 7))) != 0)
  #define FD_ZERO(p) memset((void *) (p), 0, sizeof(*(p)))
#endif

#ifndef select
  #define select(maxfdp1, readset, writeset, exceptset, timeout) lwip_select(maxfdp1, readset, writeset, exceptset, timeout)
#endif

#ifndef F_OK
  #define F_OK 0
#endif

#ifndef R_OK
  #define R_OK 4
#endif

#ifndef W_OK
  #define W_OK 2
#endif

#ifndef X_OK
  #define X_OK 1
#endif

#ifndef AF_UNIX
  #define AF_UNIX (-1)
#endif

#define access moonlight_nxdk_openssl_access
#define fileno moonlight_nxdk_openssl_fileno
#define read moonlight_nxdk_openssl_read
#define write moonlight_nxdk_openssl_write
#define close moonlight_nxdk_openssl_close
#define _close moonlight_nxdk_openssl__close
#define open moonlight_nxdk_openssl_open
#define _open moonlight_nxdk_openssl__open
#define fdopen moonlight_nxdk_openssl_fdopen
#define _fdopen moonlight_nxdk_openssl__fdopen
#define _unlink moonlight_nxdk_openssl__unlink
#define chmod moonlight_nxdk_openssl_chmod
#define getuid moonlight_nxdk_openssl_getuid
#define geteuid moonlight_nxdk_openssl_geteuid
#define getgid moonlight_nxdk_openssl_getgid
#define getegid moonlight_nxdk_openssl_getegid

  static inline int moonlight_nxdk_openssl_access(const char *path, int mode) {
    (void) path;
    (void) mode;
    return -1;
  }

  static inline int moonlight_nxdk_openssl_fileno(FILE *stream) {
    (void) stream;
    return -1;
  }

  static inline ssize_t moonlight_nxdk_openssl_read(int fd, void *buffer, size_t count) {
    return lwip_recv(fd, buffer, count, 0);
  }

  static inline ssize_t moonlight_nxdk_openssl_write(int fd, const void *buffer, size_t count) {
    return lwip_send(fd, buffer, count, 0);
  }

  static inline int moonlight_nxdk_openssl_close(int fd) {
    (void) fd;
    return -1;
  }

  static inline int moonlight_nxdk_openssl__close(int fd) {
    return moonlight_nxdk_openssl_close(fd);
  }

  static inline int moonlight_nxdk_openssl_open(const char *path, int flags, ...) {
    (void) path;
    (void) flags;
    return -1;
  }

  static inline int moonlight_nxdk_openssl__open(const char *path, int flags, ...) {
    (void) path;
    (void) flags;
    return -1;
  }

  static inline FILE *moonlight_nxdk_openssl_fdopen(int fd, const char *mode) {
    (void) fd;
    (void) mode;
    return NULL;
  }

  static inline FILE *moonlight_nxdk_openssl__fdopen(int fd, const char *mode) {
    return moonlight_nxdk_openssl_fdopen(fd, mode);
  }

  static inline int moonlight_nxdk_openssl__unlink(const char *path) {
    (void) path;
    return -1;
  }

  static inline int moonlight_nxdk_openssl_chmod(const char *path, int mode) {
    (void) path;
    (void) mode;
    return -1;
  }

  static inline unsigned int moonlight_nxdk_openssl_getuid(void) {
    return 0;
  }

  static inline unsigned int moonlight_nxdk_openssl_geteuid(void) {
    return 0;
  }

  static inline unsigned int moonlight_nxdk_openssl_getgid(void) {
    return 0;
  }

  static inline unsigned int moonlight_nxdk_openssl_getegid(void) {
    return 0;
  }

  static inline int moonlight_nxdk_openssl_gmtime_s(struct tm *result, const time_t *timer) {
    return gmtime_s(timer, result);
  }

#define gmtime_s moonlight_nxdk_openssl_gmtime_s

#ifdef __cplusplus
}
#endif
