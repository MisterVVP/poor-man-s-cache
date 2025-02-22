#pragma once
#include <cstdint>
#include <atomic>
#include <coroutine>
#include <sys/socket.h>
#include <netinet/in.h>
#include "../non_copyable.h"

namespace server {
    class AcceptConnTask : NonCopyable {
        public:
            class promise_type;
            using handle_type = std::coroutine_handle<promise_type>;
        private:
            handle_type c_handle;
            AcceptConnTask(handle_type h) : c_handle(h) {}
        public:
            class promise_type {
                public:
                    int client_fd;
                    
                    std::suspend_always initial_suspend() { return {}; }
                    std::suspend_always final_suspend() noexcept { return {}; }
                    std::suspend_always yield_value(int fd) {
                        client_fd = fd;
                        return {};
                    }
                    void unhandled_exception() {}

                    void return_void() {}

                    AcceptConnTask get_return_object() {
                        auto handle = handle_type::from_promise(*this);
                        return AcceptConnTask{handle}; 
                    }

                    promise_type(): client_fd(-1){}
                    ~promise_type() {}
            };
            int next_value() {
                auto &promise = c_handle.promise();
                promise.client_fd = -1;
                c_handle.resume();
                return promise.client_fd;
            };
            ~AcceptConnTask() {
                if (c_handle) {
                    c_handle.destroy();
                }
            };
        friend class HandreReqAwaiter;
    };

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
                while(!cancellationToken) {
                    sockaddr_in client_address;
                    socklen_t client_len = sizeof(client_address);
                    auto client_fd = accept(server_fd, (struct sockaddr*)&client_address, &client_len);
                    if (client_fd >= 0) {
                        ++activeConnectionsCounter;
                    }
                    co_yield client_fd;
                };
            }

            ConnManager(std::atomic<bool>& cToken): cancellationToken(cToken), activeConnectionsCounter(0) {}
    };
}