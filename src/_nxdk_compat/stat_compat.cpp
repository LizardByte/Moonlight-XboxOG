/**
 * @file src/_nxdk_compat/stat_compat.cpp
 * @brief Implements stat compatibility shims for nxdk.
 */
#ifdef NXDK

  #include <cstring>
  #include <sys/stat.h>

extern "C" {

  int stat(const char *path, struct stat *status) {  // NOSONAR(cpp:S833) extern "C" linkage requires external visibility
    (void) path;

    if (status != nullptr) {
      std::memset(status, 0, sizeof(*status));
    }

    return -1;
  }

  int fstat(int fd, struct stat *status) {  // NOSONAR(cpp:S833) extern "C" linkage requires external visibility
    (void) fd;

    if (status != nullptr) {
      std::memset(status, 0, sizeof(*status));
    }

    return 0;
  }

  int _stat(const char *path, struct stat *status) {
    return stat(path, status);
  }

  int _fstat(int fd, struct stat *status) {
    return fstat(fd, status);
  }

}  // extern "C"

#endif
