#include "sockutils.h"

int setNonBlocking(int fd) {
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