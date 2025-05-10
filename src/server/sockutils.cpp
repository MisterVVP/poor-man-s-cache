#include "sockutils.h"

int setNonBlocking(int fd) noexcept {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    perror("Failed to get flags for socket");
    return -1;
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    perror("Failed to set O_NONBLOCK for socket");
    return -1;
  }
  return 0;
}

int setSocketBuffers(int fd, int bufSize, int flags) noexcept {
  int result = 0;

  if ((flags & SOCK_BUF_RECEIVE) || (flags & SOCK_BUF_ALL)) {
      if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bufSize, sizeof(bufSize)) == -1) {
          perror("Failed to set SO_RCVBUF for socket");
          result = -1;
      }
  }

  if ((flags & SOCK_BUF_SEND) || (flags & SOCK_BUF_ALL)) {
      if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufSize, sizeof(bufSize)) == -1) {
          perror("Failed to set SO_SNDBUF for socket");
          result = -1;
      }
  }
  return result;
}