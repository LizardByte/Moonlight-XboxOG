#ifdef NXDK

// platform includes
#include <sys/stat.h>

extern "C" {

int _stat(const char *path, struct stat *buffer)
{
    return stat(path, buffer);
}

int _fstat(int fd, struct stat *buffer)
{
    return fstat(fd, buffer);
}

}  // extern "C"

#endif
