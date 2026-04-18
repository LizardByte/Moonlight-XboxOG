/**
 * @file src/_nxdk_compat/poll_compat.cpp
 * @brief Implements poll compatibility shims for nxdk.
 */
#ifdef NXDK

  #include <cerrno>
  #include <cstddef>
  #include <poll.h>
  #include <sys/socket.h>
  #include <sys/time.h>

/**
 * @brief Emulate poll by translating the requested events into select sets.
 *
 * @param fds File descriptor array to test.
 * @param nfds Number of entries in @p fds.
 * @param timeout Timeout in milliseconds, or a negative value to wait indefinitely.
 * @return Number of ready descriptors, zero on timeout, or -1 on error.
 */
extern "C" int poll(struct pollfd *fds, nfds_t nfds, int timeout) {
  if (fds == nullptr && nfds != 0) {
    errno = EINVAL;
    return -1;
  }

  fd_set readSet;
  fd_set writeSet;
  fd_set errorSet;
  FD_ZERO(&readSet);
  FD_ZERO(&writeSet);
  FD_ZERO(&errorSet);

  int maxFd = -1;
  std::size_t readyCount = 0;

  for (nfds_t index = 0; index < nfds; ++index) {
    fds[index].revents = 0;

    if (fds[index].fd < 0) {
      continue;
    }

    if ((fds[index].events & POLLIN) != 0) {
      FD_SET(fds[index].fd, &readSet);
    }
    if ((fds[index].events & POLLOUT) != 0) {
      FD_SET(fds[index].fd, &writeSet);
    }
    FD_SET(fds[index].fd, &errorSet);

    if (fds[index].fd > maxFd) {
      maxFd = fds[index].fd;
    }
  }

  timeval timeoutValue {};
  timeval *timeoutPointer = nullptr;
  if (timeout >= 0) {
    timeoutValue.tv_sec = timeout / 1000;
    timeoutValue.tv_usec = (timeout % 1000) * 1000;
    timeoutPointer = &timeoutValue;
  }

  if (const int selectResult = select(maxFd + 1, &readSet, &writeSet, &errorSet, timeoutPointer); selectResult <= 0) {
    return selectResult;
  }

  for (nfds_t index = 0; index < nfds; ++index) {
    if (fds[index].fd < 0) {
      continue;
    }

    if (FD_ISSET(fds[index].fd, &readSet)) {
      fds[index].revents |= POLLIN;
    }
    if (FD_ISSET(fds[index].fd, &writeSet)) {
      fds[index].revents |= POLLOUT;
    }
    if (FD_ISSET(fds[index].fd, &errorSet)) {
      fds[index].revents |= POLLERR;
    }

    if (fds[index].revents != 0) {
      ++readyCount;
    }
  }

  return static_cast<int>(readyCount);
}

#endif
