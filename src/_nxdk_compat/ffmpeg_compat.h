/**
 * @file src/_nxdk_compat/ffmpeg_compat.h
 * @brief Adapt optional FFmpeg file helpers without pretending Xbox has POSIX files.
 *
 * Moonlight supplies compressed data in memory. Normal stdio remains available;
 * descriptors, mapped files and temporary files are outside this decoder build.
 */
#pragma once
#include "ffmpeg_memory.h"

#include <errno.h>
#include <malloc.h>
#include <stdio.h>
#include <sys/types.h>

/** @brief Supply the compiler memory fence required by FFmpeg's Win32 thread probe. */
#define MemoryBarrier() __atomic_thread_fence(__ATOMIC_SEQ_CST)

/** @brief Route FFmpeg aligned allocation to a complete private implementation. */
#define _aligned_malloc moonlight_ffmpeg_aligned_malloc
/** @brief Preserve aligned buffers when FFmpeg reallocates them. */
#define _aligned_realloc moonlight_ffmpeg_aligned_realloc
/** @brief Match the private aligned allocator when releasing FFmpeg buffers. */
#define _aligned_free moonlight_ffmpeg_aligned_free

/**
 * @brief Reject an optional FFmpeg open request.
 *
 * @param path Requested path.
 * @param flags Requested access flags.
 * @param ... Unused creation mode.
 * @return Always -1 with errno set to ENOSYS.
 */
static inline int moonlight_nxdk_open(const char *path, int flags, ...) {
  (void) path;
  (void) flags;
  errno = ENOSYS;
  return -1;
}

/** @brief Keep open adaptation private to FFmpeg. */
#define open moonlight_nxdk_open

/**
 * @brief Reject an optional FFmpeg close request.
 *
 * @param descriptor Unsupported file descriptor.
 * @return Always -1 with errno set to ENOSYS.
 */
static inline int moonlight_nxdk_close(int descriptor) {
  (void) descriptor;
  errno = ENOSYS;
  return -1;
}

/** @brief Keep close adaptation private to FFmpeg. */
#define close moonlight_nxdk_close

/**
 * @brief Reject an optional FFmpeg read request.
 *
 * @param descriptor Unsupported file descriptor.
 * @param buffer Unmodified destination.
 * @param size Requested byte count.
 * @return Always -1 with errno set to ENOSYS.
 */
static inline ssize_t moonlight_nxdk_read(int descriptor, void *buffer, size_t size) {
  (void) descriptor;
  (void) buffer;
  (void) size;
  errno = ENOSYS;
  return -1;
}

/** @brief Keep read adaptation private to FFmpeg. */
#define read moonlight_nxdk_read

/**
 * @brief Reject an optional FFmpeg fdopen request.
 *
 * @param descriptor Unsupported file descriptor.
 * @param mode Requested stdio mode.
 * @return Always NULL with errno set to ENOSYS.
 */
static inline FILE *moonlight_nxdk_fdopen(int descriptor, const char *mode) {
  (void) descriptor;
  (void) mode;
  errno = ENOSYS;
  return NULL;
}

/** @brief Keep fdopen adaptation private to FFmpeg. */
#define fdopen moonlight_nxdk_fdopen

/**
 * @brief Reject an optional FFmpeg access request.
 *
 * @param path Requested path.
 * @param mode Requested permissions.
 * @return Always -1 with errno set to ENOSYS.
 */
static inline int moonlight_nxdk_access(const char *path, int mode) {
  (void) path;
  (void) mode;
  errno = ENOSYS;
  return -1;
}

/** @brief Keep access adaptation private to FFmpeg. */
#define access moonlight_nxdk_access

/**
 * @brief Reject an optional FFmpeg isatty request.
 *
 * @param descriptor Unsupported terminal descriptor.
 * @return Always 0 with errno set to ENOSYS.
 */
static inline int moonlight_nxdk_isatty(int descriptor) {
  (void) descriptor;
  errno = ENOSYS;
  return 0;
}

/** @brief Keep isatty adaptation private to FFmpeg. */
#define isatty moonlight_nxdk_isatty
