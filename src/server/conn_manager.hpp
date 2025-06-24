#pragma once
#include <cstdint>
#include <atomic>
#include <coroutine>
#include <unordered_map>
#include <vector>
#include <deque>
#include <string_view>
#include <mutex>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include "../kvs/kvs.h"
#include "../utils/time.h"
#include "../non_copyable.h"
#include "coroutines.h"
#include "sockutils.h"
#include "constants.hpp"

namespace server {
    struct ConnectionData {
        timespec lastActivity {0, 0};
        int epoll_fd = -1;
        std::vector<char> readBuffer;
        std::deque<std::string_view> pendingRequests;
        size_t bytesToErase = 0;
        ConnectionData() = default;
        ConnectionData(timespec ts, int epfd) : lastActivity(ts), epoll_fd(epfd) {
            readBuffer.reserve(READ_BUFFER_SIZE);
        }
    };

    class ConnManager {
        private:
            std::atomic<bool> cancellationToken;
            int epoll_fd;

            int registerConnection(int epoll_fd, int client_fd) {
#ifndef NDEBUG
                std::cout << "Adding client_fd = " << client_fd << " to epoll_fd = " << epoll_fd << std::endl;
#endif
                setNonBlocking(client_fd);
                epoll_event event{};
                event.events = EPOLLIN | EPOLLET;
                event.data.fd = client_fd;

                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                    perror("Failed to add client_fd to epoll");
                    closeConnection(client_fd);
                    return -1;
                }
                timespec time{0, 0};
                if (clock_gettime(CLOCK_MONOTONIC_COARSE, &time) == 0) {
                    auto [iterator, success] = connections.try_emplace(client_fd, time, epoll_fd );
                    if (!success) {
#ifndef NDEBUG
                        std::cerr << "Connection info already exists for client_fd = " << client_fd << ", epoll_fd = " << epoll_fd << std::endl;
#endif
                        return 0;
                    }
                } else {
                    perror("clock_gettime() failed when registering connection");
                    return -1;
                }
                ++activeConnectionsCounter;
                return 0; 
            };

            void validateConnections() {
                timespec now{0, 0};
                if (clock_gettime(CLOCK_MONOTONIC_COARSE, &now) == 0) {
                    for (auto it = connections.begin(); it != connections.end();) {
                        auto diff = now - it->second.lastActivity;
                        auto fd = it->first;
                        ++it;
                        if (diff.tv_sec > MAX_CONN_LIFETIME_SEC) {
                            closeConnection(fd);
                        }
                    }
                } else {
                    perror("clock_gettime() failed when validating connections");
                }
            };

        public:
            std::atomic<uint_fast32_t> activeConnectionsCounter;
            std::unordered_map<int, ConnectionData> connections;

            bool updateActivity(int fd) {
                timespec time{0, 0};
                if (clock_gettime(CLOCK_MONOTONIC_COARSE, &time) == 0) {
                    connections[fd].lastActivity = time;
                } else {
                    perror("clock_gettime() failed when updating connection activity");
                    return false;
                }
                return true;
            };

            void closeConnection(int fd) noexcept {
                if (!connections.contains(fd)) {
                    return;
                }
                if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr) == -1) {
#ifndef NDEBUG
                    perror("Error when removing socket descriptor from epoll");
#endif
                };
                if (shutdown(fd, SHUT_RDWR) == -1) {
#ifndef NDEBUG
                    perror("Error when shutting down socket descriptor");
#endif
                }
                if (close(fd) == -1) {
#ifndef NDEBUG
                    perror("Error when closing socket descriptor");
#endif
                }
                connections.erase(fd);
                --activeConnectionsCounter;
            };

            void acceptConnections(int server_fd) {
                sockaddr_in client_address;
                socklen_t client_len = sizeof(client_address);
                do {
                    auto client_fd = accept(server_fd, (struct sockaddr*)&client_address, &client_len);
                    if (client_fd >= 0) {
                        if (registerConnection(epoll_fd, client_fd) == -1) {
                            continue;
                        };
                    } else {
                        if (activeConnectionsCounter > 0) {
                            validateConnections();
                        }
                        if (errno == EINTR) {
                            perror("Failed to accept connection: interruption signal received. Retrying...");
                            continue;
                        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        } else {
                            perror("Failed to accept connection");
                        }
                    }
                } while (!cancellationToken);
            }

            void stop() {
                cancellationToken = true;
            }

            ConnManager(int epoll_fd): epoll_fd(epoll_fd), cancellationToken(false), activeConnectionsCounter(0) {}
    };
}
