#pragma once
#include <fcntl.h>
#include <stdio.h>
#include <sys/socket.h>

/// @brief Options for setSocketBuffers function flags
enum SOCK_BUF_OPTS {
  SOCK_BUF_SEND        = 0x01,
  SOCK_BUF_RECEIVE     = 0x02,
  SOCK_BUF_ALL         = 0x04,
};

/// @brief Sets socket as non blocking
/// @param fd file descriptor of socket
/// @return syscall result, -1 on error, 0 on success
int setNonBlocking(int fd) noexcept;

/// @brief Increases socket send and receive buffers
/// @param fd file descriptor of socket
/// @param bufSize buffer size to set for socket
/// @param direction 0 = send, 1 = receive, 2 = send + receive
/// @return syscall result, -1 on error, 0 on success
int setSocketBuffers(int fd, int bufSize, int flags) noexcept;
