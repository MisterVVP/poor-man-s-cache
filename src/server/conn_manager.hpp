#pragma once
#include <cstdint>
#include <atomic>
#include <mutex>
#include <coroutine>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include "../kvs/kvs.h"
#include "../non_copyable.h"
#include "coroutines.h"
#include "sockutils.h"
#include "constants.hpp"

namespace server {

    /// @brief ReadRequest result codes
    enum ReqReadOperationResult : int_fast8_t {
        Failure = -2,
        Success = 0,
        ConnectionClosed = 1,
    };

    struct ReadRequestResult {
        private:
            ReadRequestResult(ReqReadOperationResult res) : operationResult(res) {};

        public:
            std::string request;
            ReqReadOperationResult operationResult;

            ReadRequestResult(std::string&& request, ReqReadOperationResult res) : request(std::move(request)), operationResult(res) {};

            static ReadRequestResult Success(std::string req) noexcept {
                return ReadRequestResult { std::move(req), ReqReadOperationResult::Success };
            };

            static ReadRequestResult Failure() noexcept {
                return ReadRequestResult { ReqReadOperationResult::Failure };
            };

            static ReadRequestResult ConnectionClosed() noexcept {
                return ReadRequestResult { ReqReadOperationResult::ConnectionClosed };
            };
    };

    class ConnManager {
        private:
            int epoll_fd;
        public:
            std::atomic<bool>& cancellationToken;
            std::atomic<uint_fast32_t> activeConnectionsCounter;

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
                ++activeConnectionsCounter;
                return 0; 
            };

            void closeConnection(int fd) noexcept {
                auto sdRes = shutdown(fd, SHUT_RDWR);
                if (sdRes == -1) {
                    perror("Error when shutting down socket descriptor");
                }
                auto closeRes = close(fd);
                if (closeRes == -1) {
                    perror("Error when closing socket descriptor");
                }

                --activeConnectionsCounter;                
            };

            AcceptConnTask acceptConnections(int server_fd) {
                co_yield EpollStatus::Running(epoll_fd);
                sockaddr_in client_address;
                socklen_t client_len = sizeof(client_address);
                do {
                    auto client_fd = accept(server_fd, (struct sockaddr*)&client_address, &client_len);
                    if (client_fd >= 0) {
                        if(registerConnection(epoll_fd, client_fd) == -1) {
                            continue;
                        };                      
                    } else {
                        if (errno == EINTR) {
                            perror("Failed to accept connection: interruption signal received. Retrying...");
                            continue;
                        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            co_yield EpollStatus::Running(epoll_fd);
                        } else {
                            perror("Failed to accept connection");
                        }
                    }

                    if (activeConnectionsCounter > 0) {
                        co_yield EpollStatus::Processing(epoll_fd);  
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

            ConnManager(std::atomic<bool>& cToken): cancellationToken(cToken), activeConnectionsCounter(0) {
                epoll_fd = epoll_create1(0);
                if (epoll_fd == -1) {
                    throw std::system_error(errno, std::system_category(), "Failed to create epoll instance");
                }
            }
    };

    class AsyncReadTask : NonCopyable {
        public:
            class promise_type;
            using handle_type = std::coroutine_handle<promise_type>;    
        private:
            handle_type c_handle;
            AsyncReadTask(handle_type h) : c_handle(h) {};
        public:
            class promise_type {
                public:
                    std::optional<ReadRequestResult> result;
                    std::suspend_never initial_suspend() { return {}; }
                    std::suspend_always final_suspend() noexcept { return {}; }

                    void unhandled_exception() {}
                    void return_value(std::optional<ReadRequestResult> res){
                        result = res;
                    }

                    AsyncReadTask get_return_object() { return AsyncReadTask{handle_type::from_promise(*this)}; }
                    promise_type() {}
                    ~promise_type() {}
            };

            std::optional<ReadRequestResult> readResult() {
                if(!c_handle.done()){
                    c_handle.resume();
                }
                return c_handle.promise().result;
            }

            AsyncReadTask(AsyncReadTask &&art) : c_handle(art.c_handle) {
                art.c_handle = nullptr;
            }

            AsyncReadTask &operator=(AsyncReadTask &&art) {
                if (c_handle) {
                    c_handle.destroy();
                }
                c_handle = art.c_handle;
                art.c_handle = nullptr;
                return *this;
            }

            ~AsyncReadTask() {
                if (c_handle) {
                    c_handle.destroy();
                }
            };
            friend class AsyncReadAwaiter;
    };

    class AsyncReadAwaiter {
        private:
            int fd;
            char* buffer;
            size_t bufSize;
        public:
            AsyncReadAwaiter(int fd, char* buffer, size_t bufSize) : fd(fd), buffer(buffer), bufSize(bufSize) {}
            bool await_ready() const noexcept { return false; }
            void await_suspend(std::coroutine_handle<> h) {}
            
            ssize_t await_resume()
            {
                // When resumed, actually perform read
                return ::read(fd, buffer, bufSize);
            }
    };
}
