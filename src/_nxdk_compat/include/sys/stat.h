/**
 * @file src/_nxdk_compat/include/sys/stat.h
 * @brief Reject optional POSIX metadata queries; Moonlight uses Win32 file APIs.
 */
#pragma once
#include <errno.h>
#include <sys/types.h>

/** @brief Minimal metadata shape for dependency code that is never used on Xbox. */
struct stat {
  mode_t st_mode;  ///< File type and permissions; never populated by the adapter.
  off_t st_size;  ///< File size; never populated by the adapter.
};

/** @brief File type mask. */
#define S_IFMT 0170000
/** @brief Directory type bit. */
#define S_IFDIR 0040000
/** @brief Regular file type bit. */
#define S_IFREG 0100000
/** @brief Test a directory mode. */
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
/** @brief Test a regular file mode. */
#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)

/**
 * @brief Reject a POSIX path metadata query without touching its output.
 *
 * @param path Requested path.
 * @param status Unmodified output record.
 * @return Always -1 with errno set to ENOSYS.
 */
static inline int moonlight_nxdk_stat(const char *path, struct stat *status) {
  (void) path;
  (void) status;
  errno = ENOSYS;
  return -1;
}

/**
 * @brief Reject a POSIX descriptor metadata query without touching its output.
 *
 * @param descriptor Unsupported file descriptor.
 * @param status Unmodified output record.
 * @return Always -1 with errno set to ENOSYS.
 */
static inline int moonlight_nxdk_fstat(int descriptor, struct stat *status) {
  (void) descriptor;
  (void) status;
  errno = ENOSYS;
  return -1;
}

/** @brief Scope path metadata adaptation to consumers of this private header. */
#define stat(path, status) moonlight_nxdk_stat(path, status)
/** @brief Scope descriptor metadata adaptation to this private header. */
#define fstat(descriptor, status) moonlight_nxdk_fstat(descriptor, status)
