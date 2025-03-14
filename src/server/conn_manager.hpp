#pragma once
#include <cstdint>
#include <atomic>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include "../non_copyable.h"
#include "coroutines.h"
#include "sockutils.h"

namespace server {
    class ConnManager {
        public:
            std::atomic<bool>& cancellationToken;
            std::atomic<uint_fast32_t> activeConnectionsCounter;
            void closeConnection(int fd) noexcept {
                auto sdRes = shutdown(fd, SHUT_RDWR);
                if (sdRes == -1) {
                    perror("Error when shutting down socket descriptor");
                }
                auto closeRes = close(fd);
                if (closeRes == -1) {
                    perror("Error when closing socket descriptor");
                } else {
                    --activeConnectionsCounter;
                }
            };

            AcceptConnTask acceptConnections(int server_fd) {   
                co_yield EpollStatus::NotReady();
                auto epoll_fd = epoll_create1(0);
                if (epoll_fd == -1) {
                    close(server_fd);
                    throw std::system_error(errno, std::system_category(), "Failed to create epoll instance");
                    co_return EpollStatus::Terminated();
                }
                co_yield EpollStatus::Running(epoll_fd);
                sockaddr_in client_address;
                socklen_t client_len = sizeof(client_address);
                auto client_fd = -1;
                do {
                    auto client_fd = accept(server_fd, (struct sockaddr*)&client_address, &client_len);
                    if (client_fd >= 0) {
                        ++activeConnectionsCounter;
                        setNonBlocking(client_fd);
                        epoll_event event{};
                        event.events = EPOLLIN | EPOLLET;
                        event.data.fd = client_fd;
                        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                            perror("Failed to add client_fd to epoll");
                            closeConnection(client_fd);
                        } else {
                           co_yield EpollStatus::Processing(epoll_fd);
                        }
                    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        perror("Failed to accept connection");
                    }
                } while (!cancellationToken);

                if (epoll_fd >= 0) {
                    close(epoll_fd);
                }
                if (cancellationToken) {
                    co_return EpollStatus::Stopped();
                } else {
                    co_return EpollStatus::Terminated();
                }            
            }

            ConnManager(std::atomic<bool>& cToken): cancellationToken(cToken), activeConnectionsCounter(0) {}
    };
}
