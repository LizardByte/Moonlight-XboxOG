/**
 * @file src/_nxdk_compat/stat_compat.cpp
 * @brief Implements stat compatibility shims for nxdk.
 */
#ifdef NXDK

  #include <cstring>
  #include <sys/stat.h>

extern "C" {

  /**
   * @brief Stub stat for nxdk builds that do not expose a compatible host file system.
   *
   * @param path Requested path.
   * @param status Optional output populated with a zeroed status record.
   * @return Always -1 to indicate that the query is unsupported.
   */
  int stat(const char *path, struct stat *status) {  // NOSONAR(cpp:S833) extern "C" linkage requires external visibility
    (void) path;

    if (status != nullptr) {
      std::memset(status, 0, sizeof(*status));
    }

    return -1;
  }

  /**
   * @brief Stub fstat for nxdk builds that only need a successful zeroed response.
   *
   * @param fd File descriptor to inspect.
   * @param status Optional output populated with a zeroed status record.
   * @return Zero after clearing the status record when provided.
   */
  int fstat(int fd, struct stat *status) {  // NOSONAR(cpp:S833) extern "C" linkage requires external visibility
    (void) fd;

    if (status != nullptr) {
      std::memset(status, 0, sizeof(*status));
    }

    return 0;
  }

  /**
   * @brief Windows-style alias for the stat compatibility shim.
   *
   * @param path Requested path.
   * @param status Optional output populated with a zeroed status record.
   * @return Result from stat().
   */
  int _stat(const char *path, struct stat *status) {
    return stat(path, status);
  }

  /**
   * @brief Windows-style alias for the fstat compatibility shim.
   *
   * @param fd File descriptor to inspect.
   * @param status Optional output populated with a zeroed status record.
   * @return Result from fstat().
   */
  int _fstat(int fd, struct stat *status) {
    return fstat(fd, status);
  }

}  // extern "C"

#endif
