/**
 * @file src/_nxdk_compat/include/dirent.h
 * @brief Reject OpenSSL's optional POSIX directory-store backend on Xbox.
 */
#pragma once
#include <errno.h>
#include <stddef.h>
typedef struct moonlight_nxdk_directory DIR;  ///< Opaque unsupported directory iterator.

/** @brief Directory entry shape required to compile OpenSSL's optional backend. */
struct dirent {
  char d_name[256];  ///< Entry name; never populated by the adapter.
};

/**
 * @brief Reject a POSIX directory open request.
 *
 * @param path Requested directory.
 * @return Always NULL with errno set to ENOSYS.
 */
static inline DIR *opendir(const char *path) {
  (void) path;
  errno = ENOSYS;
  return NULL;
}

/**
 * @brief Reject a POSIX directory read request.
 *
 * @param directory Unsupported iterator.
 * @return Always NULL with errno set to ENOSYS.
 */
static inline struct dirent *readdir(DIR *directory) {
  (void) directory;
  errno = ENOSYS;
  return NULL;
}

/**
 * @brief Reject a POSIX directory close request.
 *
 * @param directory Unsupported iterator.
 * @return Always -1 with errno set to ENOSYS.
 */
static inline int closedir(DIR *directory) {
  (void) directory;
  errno = ENOSYS;
  return -1;
}
