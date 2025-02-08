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

int setSocketBuffers(int fd, int bufSize, int flags) noexcept
{
    if (flags & SOCK_BUF_SEND && setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bufSize, sizeof(bufSize)) == -1) {
        perror("Failed to set SO_RCVBUF for server socket");
        return -1;
    }
    if (flags & SOCK_BUF_RECEIVE && setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &bufSize, sizeof(bufSize)) == -1) {
        perror("Failed to set SO_SNDBUF for server socket");
        return -1;
    }
    return 0;
}
