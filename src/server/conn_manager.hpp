#pragma once
#include <cstdint>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>

namespace server {
    class ConnManager {
        public:
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

            int acceptConnection(int& server_fd) {
                sockaddr_in client_address;
                socklen_t client_len = sizeof(client_address);
                auto client_fd = accept(server_fd, (struct sockaddr*)&client_address, &client_len);
                if (client_fd >= 0) {
                    ++activeConnectionsCounter;
                }
                return client_fd;
            };

            ConnManager(): activeConnectionsCounter(0) {}
    };
}