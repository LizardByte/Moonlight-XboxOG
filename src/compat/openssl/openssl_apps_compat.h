#ifndef MOONLIGHT_OPENSSL_APPS_COMPAT_H
#define MOONLIGHT_OPENSSL_APPS_COMPAT_H

// platform includes
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <lwip/sockets.h>

#ifdef accept
#undef accept
#endif
#ifdef bind
#undef bind
#endif
#ifdef close
#undef close
#endif
#ifdef connect
#undef connect
#endif
#ifdef getpeername
#undef getpeername
#endif
#ifdef getsockname
#undef getsockname
#endif
#ifdef getsockopt
#undef getsockopt
#endif
#ifdef ioctl
#undef ioctl
#endif
#ifdef ioctlsocket
#undef ioctlsocket
#endif
#ifdef listen
#undef listen
#endif
#ifdef recv
#undef recv
#endif
#ifdef recvfrom
#undef recvfrom
#endif
#ifdef recvmsg
#undef recvmsg
#endif
#ifdef send
#undef send
#endif
#ifdef sendmsg
#undef sendmsg
#endif
#ifdef sendto
#undef sendto
#endif
#ifdef setsockopt
#undef setsockopt
#endif
#ifdef shutdown
#undef shutdown
#endif
#ifdef socket
#undef socket
#endif
#ifdef select
#undef select
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef F_OK
#define F_OK 0
#endif

#ifndef AF_UNIX
#define AF_UNIX (-1)
#endif

static inline int access(const char *path, int mode)
{
    (void) path;
    (void) mode;
    return -1;
}

static inline int fileno(FILE *stream)
{
    (void) stream;
    return -1;
}

static inline int open(const char *path, int flags, ...)
{
    (void) path;
    (void) flags;
    return -1;
}

static inline int _open(const char *path, int flags, ...)
{
    (void) path;
    (void) flags;
    return -1;
}

static inline FILE *fdopen(int fd, const char *mode)
{
    (void) fd;
    (void) mode;
    return NULL;
}

static inline FILE *_fdopen(int fd, const char *mode)
{
    return fdopen(fd, mode);
}

static inline int _unlink(const char *path)
{
    (void) path;
    return -1;
}

static inline int chmod(const char *path, int mode)
{
    (void) path;
    (void) mode;
    return 0;
}

static inline int gmtime_s(struct tm *result, const time_t *timer)
{
    if (result == NULL || timer == NULL) {
        return -1;
    }

    struct tm *temporary = gmtime(timer);
    if (temporary == NULL) {
        memset(result, 0, sizeof(*result));
        return -1;
    }

    *result = *temporary;
    return 0;
}

static inline int select(int maxfdp1, fd_set *readset, fd_set *writeset, fd_set *exceptset, struct timeval *timeout)
{
    return lwip_select(maxfdp1, readset, writeset, exceptset, timeout);
}

#ifdef __cplusplus
}
#endif

#endif
